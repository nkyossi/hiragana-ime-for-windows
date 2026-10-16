// Hiragana IME for Windows
//
// Copyright (c) 2026 Hiragana IME for Windows contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "TextService.h"

#include <imm.h>
#include <shellapi.h>

#include <fstream>

#include "CandidateWindow.h"
#include "EditSession.h"
#include "Globals.h"
#include "KeyMapping.h"
#include "LangBarButton.h"
#include "Log.h"

using Microsoft::WRL::ComPtr;

namespace hiragana::tsf {

namespace fs = std::filesystem;

namespace {

// TF_CONVERSIONMODE_* for each input mode, for GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION.
DWORD conversion_mode_of(Mode mode) {
    switch (mode) {
        case MODE_HIRAGANA:
            return TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE | TF_CONVERSIONMODE_ROMAN;
        case MODE_KATAKANA:
            return TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_KATAKANA | TF_CONVERSIONMODE_FULLSHAPE |
                   TF_CONVERSIONMODE_ROMAN;
        case MODE_HALFWIDTH_KATAKANA:
            return TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_KATAKANA | TF_CONVERSIONMODE_ROMAN;
        case MODE_WIDE_ALPHANUMERIC:
            return TF_CONVERSIONMODE_FULLSHAPE;
        default:
            return TF_CONVERSIONMODE_ALPHANUMERIC;
    }
}

Mode mode_of_conversion(DWORD conversion) {
    if (!(conversion & TF_CONVERSIONMODE_NATIVE))
        return (conversion & TF_CONVERSIONMODE_FULLSHAPE) ? MODE_WIDE_ALPHANUMERIC : MODE_ALPHANUMERIC;
    if (conversion & TF_CONVERSIONMODE_KATAKANA)
        return (conversion & TF_CONVERSIONMODE_FULLSHAPE) ? MODE_KATAKANA : MODE_HALFWIDTH_KATAKANA;
    return MODE_HIRAGANA;
}

fs::file_time_type stamp(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type{} : t;
}

// 半角/全角 (and Alt+` on the US keyboard) is the IME on/off hotkey of
// Windows, which toggles GUID_COMPARTMENT_KEYBOARD_OPENCLOSE; OnChange() then
// changes the mode. Handling the key in the engine as well would toggle twice.
bool is_system_toggle(WPARAM wparam) {
    return wparam == VK_KANJI || wparam == 0xF3 || wparam == 0xF4;  // VK_DBE_SBCSCHAR, VK_DBE_DBCSCHAR
}

fs::path user_dictionary_path(const Settings& s) {
    if (s.user_dictionary.empty()) return fs::path();
    fs::path p = fs::u8path(s.user_dictionary);
    return p.is_absolute() ? p : user_data_dir() / p;
}

}  // namespace

//
// SessionClient: the engine's view of the document in an edit session
//
class TextService::SessionClient : public Client {
public:
    SessionClient(TextService& service, ITfContext* context, TfEditCookie ec)
        : service_(service), context_(context), ec_(ec) {}

    ITfContext* context() const { return context_; }
    TfEditCookie ec() const { return ec_; }

    void get_surrounding_text(ustring& text, index_t& pos) override {
        text.clear();
        pos = 0;
        if (!context_) return;
        ComPtr<ITfRange> anchor = anchor_range();
        if (!anchor) return;
        text = from_utf16(text_before(anchor.Get(), 128)) + pending_;
        pos = len(text);
    }

    void delete_surrounding_text(index_t size) override {
        if (!context_ || size <= 0) return;
        // Text committed in this session is still pending; take it back first.
        index_t from_pending = (std::min)(size, len(pending_));
        pending_.resize(pending_.size() - static_cast<size_t>(from_pending));
        size -= from_pending;
        if (size <= 0) return;
        ComPtr<ITfRange> anchor = anchor_range();
        if (!anchor) return;
        // Count the UTF-16 units of the last `size` characters before the anchor.
        std::u16string before = text_before(anchor.Get(), static_cast<LONG>(size * 2 + 2));
        LONG units = 0;
        index_t chars = 0;
        size_t i = before.size();
        while (i > 0 && chars < size) {
            --i;
            ++units;
            if (0xDC00 <= before[i] && before[i] <= 0xDFFF && i > 0 && 0xD800 <= before[i - 1] &&
                before[i - 1] <= 0xDBFF) {
                --i;
                ++units;
            }
            ++chars;
        }
        ComPtr<ITfRange> range;
        if (FAILED(anchor->Clone(&range))) return;
        LONG shifted = 0;
        HRESULT hr1 = range->ShiftStart(ec_, -units, &shifted, nullptr);
        if (service_.composition_) {
            // Some applications (RichEdit) refuse to change text outside the
            // composition while it is active; take the text into it first.
            service_.composition_->ShiftStart(ec_, range.Get());
        }
        HRESULT hr2 = range->SetText(ec_, 0, L"", 0);
        LOG_DEBUG("delete_surrounding_text(%td): before=\"%s\" units=%ld shifted=%ld hr=%#lx/%#lx", size,
                  to_utf8(from_utf16(before)).c_str(), units, shifted, static_cast<unsigned long>(hr1),
                  static_cast<unsigned long>(hr2));
    }

    // Committed text is written when the session ends or the preedit is
    // updated: some applications (RichEdit) refuse to delete text that has
    // left the composition in the same edit session, which the engine does,
    // e.g. when right Shift turns a just committed katakana into katakana again.
    void commit_text(const ustring& text) override {
        if (context_) pending_ += text;
    }

    void flush_pending() {
        if (!context_ || pending_.empty()) return;
        std::wstring t = to_wide(pending_);
        pending_.clear();
        if (!service_.composition_ && !start_composition()) return;
        ComPtr<ITfRange> range;
        if (FAILED(service_.composition_->GetRange(&range))) return;
        // Put the text at the start of the composition and move the start of the
        // composition after it, which commits the text.
        std::wstring all = t + read_text(range.Get());
        range->SetText(ec_, 0, all.c_str(), static_cast<LONG>(all.size()));
        ComPtr<ITfRange> rest;
        if (FAILED(range->Clone(&rest))) return;
        LONG shifted = 0;
        rest->ShiftStart(ec_, static_cast<LONG>(t.size()), &shifted, nullptr);
        ComPtr<ITfRange> committed;
        if (SUCCEEDED(range->Clone(&committed))) {
            committed->Collapse(ec_, TF_ANCHOR_START);
            committed->ShiftEnd(ec_, static_cast<LONG>(t.size()), &shifted, nullptr);
            clear_attributes(committed.Get());
        }
        HRESULT hr = service_.composition_->ShiftStart(ec_, rest.Get());
        LOG_DEBUG("commit_text(\"%s\"): composition was \"%s\", ShiftStart hr=%#lx", to_utf8(from_wide(t)).c_str(),
                  to_utf8(from_wide(all.substr(t.size()))).c_str(), static_cast<unsigned long>(hr));
        if (!preedit_pending_) {
            // Nothing else is composed; keep the caret after the committed text.
            set_caret(rest.Get(), TF_ANCHOR_END);
        }
    }

    void update_preedit(const Preedit& preedit) override {
        preedit_pending_ = false;
        if (!context_) return;
        flush_pending();
        std::wstring text = to_wide(preedit.text());
        if (text.empty()) {
            if (service_.composition_) {
                ComPtr<ITfRange> range;
                if (SUCCEEDED(service_.composition_->GetRange(&range))) {
                    clear_attributes(range.Get());
                    range->SetText(ec_, 0, L"", 0);
                    set_caret(range.Get(), TF_ANCHOR_END);
                }
                service_.end_composition(ec_);
            }
            return;
        }
        if (!service_.composition_ && !start_composition()) return;
        ComPtr<ITfRange> range;
        if (FAILED(service_.composition_->GetRange(&range))) return;
        range->SetText(ec_, 0, text.c_str(), static_cast<LONG>(text.size()));
        set_attributes(range.Get(), preedit);
        set_caret(range.Get(), TF_ANCHOR_END);
    }

    void update_lookup_table(const LookupTable& table, bool visible) override {
        service_.store_lookup_table(table, visible);
    }

    void mode_changed(Mode mode) override { service_.on_mode_changed(mode); }

    // Called before the engine processes a key: the composition is restarted
    // from the engine's preedit, so mark it as pending.
    void expect_preedit() { preedit_pending_ = true; }

    ComPtr<ITfRange> composition_range() {
        ComPtr<ITfRange> range;
        if (service_.composition_) service_.composition_->GetRange(&range);
        return range;
    }

private:
    // The position the engine regards as the cursor: the start of the
    // composition, or the start of the selection.
    ComPtr<ITfRange> anchor_range() {
        ComPtr<ITfRange> range;
        if (service_.composition_) {
            ComPtr<ITfRange> comp;
            if (SUCCEEDED(service_.composition_->GetRange(&comp)) && SUCCEEDED(comp->Clone(&range))) {
                range->Collapse(ec_, TF_ANCHOR_START);
                return range;
            }
        }
        TF_SELECTION sel = {};
        ULONG fetched = 0;
        if (SUCCEEDED(context_->GetSelection(ec_, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched == 1) {
            range.Attach(sel.range);
            range->Collapse(ec_, TF_ANCHOR_START);
        }
        return range;
    }

    std::u16string text_before(ITfRange* anchor, LONG count) {
        ComPtr<ITfRange> range;
        if (FAILED(anchor->Clone(&range))) return {};
        LONG shifted = 0;
        if (FAILED(range->ShiftStart(ec_, -count, &shifted, nullptr))) return {};
        std::wstring buffer(static_cast<size_t>(count), L'\0');
        ULONG fetched = 0;
        if (FAILED(range->GetText(ec_, 0, buffer.data(), static_cast<ULONG>(buffer.size()), &fetched))) return {};
        buffer.resize(fetched);
        return std::u16string(buffer.begin(), buffer.end());
    }

    std::wstring read_text(ITfRange* range) {
        ComPtr<ITfRange> r;
        if (FAILED(range->Clone(&r))) return {};
        std::wstring text;
        wchar_t buffer[256];
        for (;;) {
            ULONG fetched = 0;
            if (FAILED(r->GetText(ec_, TF_TF_MOVESTART, buffer, static_cast<ULONG>(std::size(buffer)), &fetched)))
                break;
            text.append(buffer, fetched);
            if (fetched < std::size(buffer)) break;
        }
        return text;
    }

    bool start_composition() {
        ComPtr<ITfInsertAtSelection> insert;
        ComPtr<ITfContextComposition> composition_context;
        if (FAILED(context_->QueryInterface(IID_PPV_ARGS(&insert))) ||
            FAILED(context_->QueryInterface(IID_PPV_ARGS(&composition_context))))
            return false;
        ComPtr<ITfRange> range;
        if (FAILED(insert->InsertTextAtSelection(ec_, TF_IAS_QUERYONLY, nullptr, 0, &range)) || !range)
            return false;
        ComPtr<ITfComposition> composition;
        if (FAILED(composition_context->StartComposition(ec_, range.Get(),
                                                         static_cast<ITfCompositionSink*>(&service_),
                                                         &composition)) ||
            !composition)
            return false;
        // A selection the composition starts on is replaced by the typed text.
        ComPtr<ITfRange> initial;
        if (SUCCEEDED(composition->GetRange(&initial))) initial->SetText(ec_, 0, L"", 0);
        service_.composition_ = composition;
        service_.composition_context_ = context_;
        return true;
    }

    void set_caret(ITfRange* range, TfAnchor anchor) {
        ComPtr<ITfRange> caret;
        if (FAILED(range->Clone(&caret))) return;
        caret->Collapse(ec_, anchor);
        TF_SELECTION sel = {};
        sel.range = caret.Get();
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        context_->SetSelection(ec_, 1, &sel);
    }

    void clear_attributes(ITfRange* range) {
        ComPtr<ITfProperty> property;
        if (SUCCEEDED(context_->GetProperty(GUID_PROP_ATTRIBUTE, &property))) property->Clear(ec_, range);
    }

    void set_attributes(ITfRange* range, const Preedit& preedit) {
        ComPtr<ITfProperty> property;
        if (FAILED(context_->GetProperty(GUID_PROP_ATTRIBUTE, &property))) return;
        property->Clear(ec_, range);
        LONG offset = 0;
        for (const auto& seg : preedit.segments) {
            LONG length = static_cast<LONG>(utf16_length(seg.text));
            if (length == 0) continue;
            Attribute a = seg.style == PreeditSegment::Candidate ? Attribute::Candidate
                          : seg.style == PreeditSegment::Locked  ? Attribute::Locked
                                                                 : Attribute::Input;
            ComPtr<ITfRange> part;
            if (SUCCEEDED(range->Clone(&part))) {
                LONG shifted = 0;
                part->Collapse(ec_, TF_ANCHOR_START);
                part->ShiftEnd(ec_, offset + length, &shifted, nullptr);
                part->ShiftStart(ec_, offset, &shifted, nullptr);
                VARIANT v;
                VariantInit(&v);
                v.vt = VT_I4;
                v.lVal = static_cast<LONG>(service_.atoms_[static_cast<size_t>(a)]);
                property->SetValue(ec_, part.Get(), &v);
            }
            offset += length;
        }
    }

    TextService& service_;
    ITfContext* context_;
    TfEditCookie ec_;
    bool preedit_pending_ = false;
    ustring pending_;  // committed text not written yet
};

//
// DirectAdapter: the engine's view of an editor connected through DirectClient,
// which receives the same requests as an IBus client.
//
class TextService::DirectAdapter : public Client {
public:
    DirectAdapter(TextService& service, direct::DirectClient* client) : service_(service), client_(client) {}

    void get_surrounding_text(ustring& text, index_t& pos) override {
        std::wstring buffer(4096, L'\0');
        int cursor = 0;
        int length = client_->GetSurroundingText(buffer.data(), static_cast<int>(buffer.size()), &cursor);
        if (length < 0) length = 0;
        if (static_cast<int>(buffer.size()) <= length) length = static_cast<int>(buffer.size()) - 1;
        buffer.resize(static_cast<size_t>(length));
        cursor = (std::max)(0, (std::min)(cursor, length));
        text = from_wide(buffer);
        pos = len(from_wide(std::wstring_view(buffer.data(), static_cast<size_t>(cursor))));
    }

    void delete_surrounding_text(index_t size) override {
        if (0 < size) client_->DeleteSurroundingText(static_cast<int>(-size), static_cast<int>(size));
    }

    void commit_text(const ustring& text) override {
        std::wstring w = to_wide(text);
        if (!w.empty()) client_->CommitText(w.c_str(), static_cast<int>(w.size()));
    }

    void update_preedit(const Preedit& preedit) override {
        std::wstring text;
        std::vector<direct::PreeditSpan> spans;
        for (const auto& seg : preedit.segments) {
            std::wstring w = to_wide(seg.text);
            if (w.empty()) continue;
            int style = seg.style == PreeditSegment::Candidate ? direct::kCandidate
                        : seg.style == PreeditSegment::Locked  ? direct::kLocked
                                                               : direct::kInput;
            spans.push_back({static_cast<int>(text.size()), static_cast<int>(w.size()), style});
            text += w;
        }
        // As update_preedit_text() in engine.py, the cursor is at the end.
        client_->UpdatePreedit(text.c_str(), static_cast<int>(text.size()), spans.data(),
                               static_cast<int>(spans.size()), static_cast<int>(text.size()));
    }

    void update_lookup_table(const LookupTable& table, bool visible) override {
        service_.store_lookup_table(table, visible);
    }

    void mode_changed(Mode mode) override { service_.on_mode_changed(mode); }

private:
    TextService& service_;
    direct::DirectClient* client_;
};

//
// TextService
//
TextService::TextService() {
    DllAddRef();
    host_.service = this;
}

TextService::~TextService() { DllRelease(); }

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (riid == IID_ITfThreadMgrEventSink) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (riid == IID_ITfThreadFocusSink) {
        *ppv = static_cast<ITfThreadFocusSink*>(this);
    } else if (riid == IID_ITfTextEditSink) {
        *ppv = static_cast<ITfTextEditSink*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (riid == IID_ITfCompositionSink) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (riid == IID_ITfCompartmentEventSink) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    } else if (riid == IID_ITfDisplayAttributeProvider) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return InterlockedIncrement(&ref_); }

STDMETHODIMP_(ULONG) TextService::Release() {
    LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return n;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* thread_mgr, TfClientId client_id) {
    return ActivateEx(thread_mgr, client_id, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id, DWORD) {
    try {
        init_logging();
        thread_mgr_ = thread_mgr;
        client_id_ = client_id;

        ComPtr<ITfSource> source;
        if (SUCCEEDED(thread_mgr_.As(&source))) {
            source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this),
                               &thread_mgr_cookie_);
            source->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this),
                               &thread_focus_cookie_);
        }
        ComPtr<ITfKeystrokeMgr> keystroke;
        if (SUCCEEDED(thread_mgr_.As(&keystroke))) {
            keystroke_advised_ =
                SUCCEEDED(keystroke->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE));
        }

        ComPtr<ITfCategoryMgr> category;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&category)))) {
            for (size_t i = 0; i < static_cast<size_t>(Attribute::Count); ++i)
                category->RegisterGUID(attribute_guid(static_cast<Attribute>(i)), &atoms_[i]);
        }

        create_engine();

        candidate_window_ = std::make_unique<CandidateWindow>([this](size_t i) { on_candidate_clicked(i); },
                                                              [this](int d) { on_candidate_wheel(d); });

        ComPtr<ITfCompartmentMgr> compartments;
        if (SUCCEEDED(thread_mgr_.As(&compartments))) {
            ComPtr<ITfCompartment> c;
            ComPtr<ITfSource> s;
            if (SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &c)) &&
                SUCCEEDED(c.As(&s)))
                s->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this),
                              &openclose_cookie_);
            c.Reset();
            s.Reset();
            if (SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &c)) &&
                SUCCEEDED(c.As(&s)))
                s->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this),
                              &conversion_cookie_);
        }

        LangBarButton::Callbacks callbacks;
        callbacks.mode = [this]() -> char32_t { return engine_ ? engine_->get_mode() : MODE_ALPHANUMERIC; };
        callbacks.toggle = [this] {
            if (engine_) change_mode(engine_->get_mode() == MODE_ALPHANUMERIC ? MODE_HIRAGANA : MODE_ALPHANUMERIC);
        };
        callbacks.menu = [this](POINT pt) { show_menu(pt); };
        langbar_ = new LangBarButton(std::move(callbacks));
        ComPtr<ITfLangBarItemMgr> langbar_mgr;
        if (SUCCEEDED(thread_mgr_.As(&langbar_mgr))) langbar_mgr->AddItem(langbar_);

        if (engine_) {
            run_without_document([this] {
                std::optional<bool> caps;
                if (engine_->is_onoff_by_caps()) caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
                engine_->enable(caps);
            });
            sync_compartments(engine_->get_mode());
        }

        ComPtr<ITfDocumentMgr> focus;
        if (SUCCEEDED(thread_mgr_->GetFocus(&focus)) && focus) OnSetFocus(focus.Get(), nullptr);
        return S_OK;
    } catch (const std::exception& e) {
        LOG_ERROR("ActivateEx: %s", e.what());
        Deactivate();
        return E_FAIL;
    } catch (...) {
        Deactivate();
        return E_FAIL;
    }
}

STDMETHODIMP TextService::Deactivate() {
    try {
        leave_direct();
        if (composition_) commit_composition(TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
        if (engine_) {
            run_without_document([this] { engine_->disable(); });
        }
        unadvise_text_edit_sink();
        if (thread_mgr_) {
            ComPtr<ITfSource> source;
            if (SUCCEEDED(thread_mgr_.As(&source))) {
                if (thread_mgr_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(thread_mgr_cookie_);
                if (thread_focus_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(thread_focus_cookie_);
            }
            thread_mgr_cookie_ = thread_focus_cookie_ = TF_INVALID_COOKIE;
            ComPtr<ITfKeystrokeMgr> keystroke;
            if (keystroke_advised_ && SUCCEEDED(thread_mgr_.As(&keystroke))) keystroke->UnadviseKeyEventSink(client_id_);
            keystroke_advised_ = false;
            ComPtr<ITfCompartmentMgr> compartments;
            if (SUCCEEDED(thread_mgr_.As(&compartments))) {
                const std::pair<const GUID*, DWORD*> sinks[] = {
                    {&GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &openclose_cookie_},
                    {&GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &conversion_cookie_}};
                for (const auto& [guid, cookie] : sinks) {
                    ComPtr<ITfCompartment> c;
                    ComPtr<ITfSource> s;
                    if (*cookie != TF_INVALID_COOKIE && SUCCEEDED(compartments->GetCompartment(*guid, &c)) &&
                        SUCCEEDED(c.As(&s)))
                        s->UnadviseSink(*cookie);
                    *cookie = TF_INVALID_COOKIE;
                }
            }
            if (langbar_) {
                ComPtr<ITfLangBarItemMgr> langbar_mgr;
                if (SUCCEEDED(thread_mgr_.As(&langbar_mgr))) langbar_mgr->RemoveItem(langbar_);
            }
        }
        if (langbar_) {
            langbar_->Release();
            langbar_ = nullptr;
        }
        candidate_window_.reset();
        if (menu_owner_) {
            DestroyWindow(menu_owner_);
            menu_owner_ = nullptr;
        }
        engine_.reset();
        remote_model_.reset();
        composition_.Reset();
        composition_context_.Reset();
        thread_mgr_.Reset();
        client_id_ = TF_CLIENTID_NULL;
    } catch (...) {
    }
    return S_OK;
}

void TextService::create_engine() {
    try {
        settings_ = Settings::load(settings_path());
        settings_time_ = stamp(settings_path());
        user_dictionary_time_ = stamp(user_dictionary_path(settings_));
        last_settings_check_ = GetTickCount();
        Engine::Paths paths{module_dir(), user_data_dir()};
        engine_ = std::make_unique<Engine>(paths, settings_, xkb());
        LOG_INFO("engine created: xkb=%s layout=%s dictionary=%s", xkb().c_str(), settings_.layout.c_str(),
                 settings_.dictionary.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("could not create the engine: %s", e.what());
        engine_.reset();
    }
}

std::string TextService::xkb() const {
    return settings_.keyboard == "auto" || settings_.keyboard.empty() ? detect_xkb() : settings_.keyboard;
}

void TextService::update_model() {
    if (!engine_) return;
    if (!settings_.use_llm) {
        if (remote_model_) {
            engine_->set_model(nullptr);
            remote_model_.reset();
        }
        return;
    }
    if (!remote_model_) remote_model_ = std::make_shared<RemoteModel>(module_dir() / L"hiragana-llm.exe");
    if (remote_model_->ready())
        engine_->set_model(remote_model_);
    else
        engine_->set_model(nullptr);
}

void TextService::refresh_settings(bool force) {
    if (!engine_) return;
    DWORD now = GetTickCount();
    if (!force && now - last_settings_check_ < 1000) return;
    last_settings_check_ = now;
    auto settings_time = stamp(settings_path());
    auto dictionary_time = stamp(user_dictionary_path(settings_));
    if (settings_time != settings_time_) {
        settings_time_ = settings_time;
        settings_ = Settings::load(settings_path());
        engine_->apply_settings(settings_, xkb());
        dictionary_time = stamp(user_dictionary_path(settings_));
        user_dictionary_time_ = dictionary_time;
    }
    if (dictionary_time != user_dictionary_time_) {
        user_dictionary_time_ = dictionary_time;
        engine_->reload_dictionary();
    }
}

HRESULT TextService::run_session(ITfContext* context, DWORD flags, const std::function<void(SessionClient&)>& fn) {
    if (!context || !engine_) return E_FAIL;
    ComPtr<TextService> self(this);
    ComPtr<ITfContext> ctx(context);
    auto* session = new EditSession([self, ctx, fn](TfEditCookie ec) -> HRESULT {
        TextService* service = self.Get();
        if (!service->engine_) return E_FAIL;
        SessionClient client(*service, ctx.Get(), ec);
        service->engine_->set_client(&client);
        try {
            fn(client);
        } catch (...) {
            service->engine_->set_client(nullptr);
            throw;
        }
        service->engine_->set_client(nullptr);
        service->finish_session(client);
        return S_OK;
    });
    HRESULT session_hr = E_FAIL;
    HRESULT hr = context->RequestEditSession(client_id_, session, flags, &session_hr);
    session->Release();
    if (FAILED(hr)) return hr;
    // TF_S_ASYNC means the session will run later.
    return session_hr == TF_S_ASYNC ? TF_S_ASYNC : session_hr;
}

void TextService::run_without_document(const std::function<void()>& fn) {
    if (!engine_) return;
    SessionClient client(*this, nullptr, 0);
    engine_->set_client(&client);
    try {
        fn();
    } catch (...) {
        engine_->set_client(nullptr);
        throw;
    }
    engine_->set_client(nullptr);
    finish_session(client);
}

void TextService::finish_session(SessionClient& client) {
    client.flush_pending();
    if (!table_changed_) return;
    table_changed_ = false;
    RECT rect = {};
    HWND owner = nullptr;
    bool located = false;
    if (table_visible_ && client.context()) {
        ComPtr<ITfContextView> view;
        ComPtr<ITfRange> range = client.composition_range();
        if (range && SUCCEEDED(client.context()->GetActiveView(&view))) {
            BOOL clipped = FALSE;
            located = SUCCEEDED(view->GetTextExt(client.ec(), range.Get(), &rect, &clipped)) &&
                      (rect.left != 0 || rect.top != 0 || rect.bottom != 0);
            view->GetWnd(&owner);
        }
    }
    show_candidates(located ? &rect : nullptr, owner);
}

void TextService::store_lookup_table(const LookupTable& table, bool visible) {
    table_visible_ = visible && 0 < table.get_number_of_candidates();
    table_page_.clear();
    if (table_visible_) {
        size_t page_size = table.get_page_size();
        size_t start = table.page_start();
        for (size_t i = start; i < start + page_size && i < table.get_number_of_candidates(); ++i)
            table_page_.push_back(to_wide(table.get_candidate(i)));
        table_selected_ = table.get_cursor_pos() - start;
        table_page_index_ = table.get_cursor_pos() / page_size;
        table_page_count_ = (table.get_number_of_candidates() + page_size - 1) / page_size;
    }
    table_changed_ = true;
}

void TextService::show_candidates(const RECT* text_rect, HWND owner) {
    if (!candidate_window_) return;
    if (!table_visible_ || !engine_ || !engine_->is_enabled()) {
        candidate_window_->hide();
        return;
    }
    candidate_window_->set_candidates(table_page_, table_selected_, table_page_index_, table_page_count_);
    RECT rect = {};
    bool located = false;
    if (text_rect) {
        rect = *text_rect;
        located = true;
    }
    if (!located) {
        // Fall back to the caret of the focused window.
        GUITHREADINFO gti = {sizeof gti};
        if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret) {
            rect = gti.rcCaret;
            MapWindowPoints(gti.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rect), 2);
            located = true;
        }
    }
    if (!located) {
        POINT pt;
        GetCursorPos(&pt);
        rect = {pt.x, pt.y, pt.x, pt.y + 16};
    }
    candidate_window_->show_at(rect, owner);
}

bool TextService::context_accepts_input(ITfContext* context) {
    if (!context) return false;
    TF_STATUS status = {};
    if (FAILED(context->GetStatus(&status))) return false;
    if (status.dwDynamicFlags & TS_SD_READONLY) return false;
    // Password fields and empty contexts disable the keyboard text services.
    ComPtr<ITfCompartmentMgr> compartments;
    if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&compartments)))) {
        for (const GUID* guid : {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT}) {
            ComPtr<ITfCompartment> c;
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(compartments->GetCompartment(*guid, &c)) && SUCCEEDED(c->GetValue(&v)) &&
                v.vt == VT_I4 && v.lVal)
                return false;
        }
    }
    return true;
}

BOOL TextService::handle_key(ITfContext* context, WPARAM wparam, LPARAM lparam, bool release) {
    if (!engine_ || is_system_toggle(wparam)) return FALSE;
    HWND focus = GetFocus();
    if (direct::DirectClient* client = direct::query_client(focus, &host_))
        return handle_direct_key(focus, client, wparam, lparam, release);
    if (direct_hwnd_) leave_direct();
    if (!context_accepts_input(context)) return FALSE;
    KeyEvent k = translate_key(wparam, lparam, release, xkb());
    // The application must not get a swapped key the engine leaves: it is
    // not the key the user meant.
    BOOL swapped = swap_key(wparam, release, k) && !release;
    bool caps_release = k.keyval == keysym::Caps_Lock && release && engine_->is_onoff_by_caps();
    if (!caps_release && engine_->is_trivial_key(k.keyval, k.keycode, k.state)) {
        engine_->note_trivial_key(k.keyval, k.keycode, k.state);
        return swapped;
    }
    TF_STATUS status = {};
    context->GetStatus(&status);
    bool supported = !(status.dwStaticFlags & TS_SS_TRANSITORY);
    LOG_DEBUG("context status: dynamic=%#lx static=%#lx", static_cast<unsigned long>(status.dwDynamicFlags),
              static_cast<unsigned long>(status.dwStaticFlags));
    bool eaten = false;
    HRESULT hr = run_session(context, TF_ES_SYNC | TF_ES_READWRITE, [&](SessionClient& client) {
        refresh_settings();
        update_model();
        engine_->set_surrounding_supported(supported);
        client.expect_preedit();
        if (caps_release) {
            // Windows has toggled Caps Lock by now.
            engine_->set_mode_by_caps_lock((GetKeyState(VK_CAPITAL) & 1) != 0);
            eaten = false;
        } else {
            eaten = engine_->process_key_event(k.keyval, k.keycode, k.state);
        }
    });
    if (FAILED(hr)) {
        LOG_WARNING("RequestEditSession failed: %#lx", static_cast<unsigned long>(hr));
        return FALSE;
    }
    return eaten || swapped ? TRUE : FALSE;
}

bool TextService::swap_key(WPARAM wparam, bool release, KeyEvent& k) {
    if (release) {
        // The release of a key swapped when it was pressed, whatever the mode is now.
        if (swapped_keys_.erase(wparam) == 0) return false;
        return tsf::swap_key(k, settings_.key_swaps);
    }
    swapped_keys_.erase(wparam);
    if (settings_.key_swaps.empty()) return false;
    // In the alphanumeric mode the keys keep their own roles, and so do the
    // shortcuts with Ctrl or Alt.
    if (k.state & (keysym::CONTROL_MASK | keysym::MOD1_MASK)) return false;
    if (!engine_->is_enabled() && !engine_->has_pending_input()) return false;
    if (!tsf::swap_key(k, settings_.key_swaps)) return false;
    swapped_keys_.insert(wparam);
    return true;
}

direct::DirectClient* TextService::direct_client() {
    if (!direct_hwnd_ || !IsWindow(direct_hwnd_)) return nullptr;
    return direct::query_client(direct_hwnd_, &host_);
}

void TextService::run_direct(direct::DirectClient* client, const std::function<void()>& fn) {
    if (!engine_ || !client) return;
    DirectAdapter adapter(*this, client);
    engine_->set_client(&adapter);
    try {
        fn();
    } catch (...) {
        engine_->set_client(nullptr);
        throw;
    }
    engine_->set_client(nullptr);
    if (table_changed_) {
        table_changed_ = false;
        RECT rect = {};
        bool located = table_visible_ && client->GetCaretRect(&rect);
        show_candidates(located ? &rect : nullptr, direct_hwnd_ ? GetAncestor(direct_hwnd_, GA_ROOT) : nullptr);
    }
}

void TextService::leave_direct() {
    if (!direct_hwnd_) return;
    direct::DirectClient* client = direct_client();
    if (engine_ && engine_->has_pending_input()) {
        if (client)
            run_direct(client, [this] { engine_->commit_all(); });
        else
            run_without_document([this] { engine_->composition_terminated(); });
    }
    if (client) direct::query_client(direct_hwnd_, nullptr);  // tells the editor to forget the host
    direct_hwnd_ = nullptr;
    if (candidate_window_) candidate_window_->hide();
    table_visible_ = false;
}

BOOL TextService::handle_direct_key(HWND hwnd, direct::DirectClient* client, WPARAM wparam, LPARAM lparam,
                                    bool release) {
    if (direct_hwnd_ != hwnd) {
        if (composition_) commit_composition(TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
        if (direct_hwnd_) leave_direct();
        direct_hwnd_ = hwnd;
        run_without_document([this] { engine_->focus_in(); });
        client = direct::query_client(hwnd, &host_);
        if (!client) return FALSE;
    }
    KeyEvent k = translate_key(wparam, lparam, release, xkb());
    BOOL swapped = swap_key(wparam, release, k) && !release;
    bool caps_release = k.keyval == keysym::Caps_Lock && release && engine_->is_onoff_by_caps();
    if (!caps_release && engine_->is_trivial_key(k.keyval, k.keycode, k.state)) {
        engine_->note_trivial_key(k.keyval, k.keycode, k.state);
        return swapped;
    }
    bool eaten = false;
    run_direct(client, [&] {
        refresh_settings();
        update_model();
        engine_->set_surrounding_supported(true);
        if (caps_release) {
            engine_->set_mode_by_caps_lock((GetKeyState(VK_CAPITAL) & 1) != 0);
        } else {
            eaten = engine_->process_key_event(k.keyval, k.keycode, k.state);
        }
    });
    return eaten || swapped ? TRUE : FALSE;
}

void __stdcall TextService::Host::Reset(direct::DirectClient* client) {
    try {
        if (service && service->engine_ && client) service->run_direct(client, [this] { service->engine_->reset(); });
    } catch (...) {
    }
}

void __stdcall TextService::Host::CommitAll(direct::DirectClient* client) {
    try {
        if (service && service->engine_ && client && service->engine_->has_pending_input())
            service->run_direct(client, [this] { service->engine_->commit_all(); });
    } catch (...) {
    }
}

BOOL TextService::test_key(ITfContext* context, WPARAM wparam, LPARAM lparam, bool release) {
    // The repeat count in lparam can differ between the calls, so compare the
    // scan code and the extended key flag only.
    const LPARAM mask = 0x01FF0000;
    if (tested_key_.valid && tested_key_.wparam == wparam && (tested_key_.lparam & mask) == (lparam & mask) &&
        tested_key_.release == release) {
        // An IMM32 (CUAS) window can ask about the same key more than once;
        // do not run the engine again.
        return tested_key_.eaten;
    }
    tested_key_.valid = false;
    // Give the engine's real answer here. In IMM32 windows (through CUAS) a key
    // the text service has claimed in the test is replaced by VK_PROCESSKEY and
    // does not reach the window even if OnKeyDown leaves it, so a prediction is
    // not enough. OnKeyDown then returns this cached answer without processing
    // the key a second time.
    BOOL eaten = handle_key(context, wparam, lparam, release);
    tested_key_ = {true, wparam, lparam, release, eaten};
    return eaten;
}

BOOL TextService::process_key(ITfContext* context, WPARAM wparam, LPARAM lparam, bool release) {
    // The repeat count in lparam can differ between the test and the key event,
    // so compare the scan code and the extended key flag only.
    const LPARAM mask = 0x01FF0000;
    if (tested_key_.valid && tested_key_.wparam == wparam && (tested_key_.lparam & mask) == (lparam & mask) &&
        tested_key_.release == release) {
        tested_key_.valid = false;
        return tested_key_.eaten;
    }
    tested_key_.valid = false;
    return handle_key(context, wparam, lparam, release);
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    try {
        *eaten = test_key(context, wparam, lparam, false);
    } catch (...) {
        *eaten = FALSE;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    try {
        *eaten = test_key(context, wparam, lparam, true);
    } catch (...) {
        *eaten = FALSE;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    try {
        *eaten = process_key(context, wparam, lparam, false);
    } catch (const std::exception& e) {
        LOG_ERROR("OnKeyDown: %s", e.what());
        *eaten = FALSE;
    } catch (...) {
        *eaten = FALSE;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    try {
        *eaten = process_key(context, wparam, lparam, true);
    } catch (const std::exception& e) {
        LOG_ERROR("OnKeyUp: %s", e.what());
        *eaten = FALSE;
    } catch (...) {
        *eaten = FALSE;
    }
    return S_OK;
}

void TextService::end_composition(TfEditCookie ec) {
    if (composition_) {
        composition_->EndComposition(ec);
        composition_.Reset();
        composition_context_.Reset();
    }
}

void TextService::commit_composition(DWORD flags) {
    if (!composition_context_) return;
    ComPtr<ITfContext> context = composition_context_;
    HRESULT hr = run_session(context.Get(), flags, [this](SessionClient& client) {
        if (engine_->has_pending_input()) {
            engine_->commit_all();
        } else if (composition_) {
            // The engine has been reset meanwhile; keep the text as it is shown.
            ComPtr<ITfRange> range = client.composition_range();
            ComPtr<ITfProperty> property;
            if (range && SUCCEEDED(client.context()->GetProperty(GUID_PROP_ATTRIBUTE, &property)))
                property->Clear(client.ec(), range.Get());
        }
        end_composition(client.ec());
    });
    if (FAILED(hr) || hr == TF_S_ASYNC) {
        // The session may run later; forget the conversion now so that the next
        // document starts clean. The deferred session keeps the composed text.
        run_without_document([this] { engine_->composition_terminated(); });
    }
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) {
    try {
        leave_direct();
        if (composition_) commit_composition(TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
        if (candidate_window_) candidate_window_->hide();
        table_visible_ = false;
        if (engine_) {
            run_without_document([this] {
                engine_->focus_out();
                refresh_settings(true);
                engine_->focus_in();
            });
        }
        unadvise_text_edit_sink();
        if (focus) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(focus->GetTop(&context)) && context) advise_text_edit_sink(context.Get());
        }
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP TextService::OnSetThreadFocus() {
    try {
        if (candidate_window_ && table_visible_ && composition_) candidate_window_->set_candidates(
            table_page_, table_selected_, table_page_index_, table_page_count_);
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    try {
        if (candidate_window_) candidate_window_->hide();
    } catch (...) {
    }
    return S_OK;
}

void TextService::advise_text_edit_sink(ITfContext* context) {
    ComPtr<ITfSource> source;
    if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&source))) &&
        SUCCEEDED(source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &text_edit_cookie_))) {
        text_edit_context_ = context;
    } else {
        text_edit_cookie_ = TF_INVALID_COOKIE;
    }
}

void TextService::unadvise_text_edit_sink() {
    if (text_edit_context_ && text_edit_cookie_ != TF_INVALID_COOKIE) {
        ComPtr<ITfSource> source;
        if (SUCCEEDED(text_edit_context_.As(&source))) source->UnadviseSink(text_edit_cookie_);
    }
    text_edit_context_.Reset();
    text_edit_cookie_ = TF_INVALID_COOKIE;
}

STDMETHODIMP TextService::OnEndEdit(ITfContext* context, TfEditCookie ec, ITfEditRecord* record) {
    try {
        if (!composition_ || !record || context != composition_context_.Get()) return S_OK;
        BOOL changed = FALSE;
        if (FAILED(record->GetSelectionStatus(&changed)) || !changed) return S_OK;
        // Commit the conversion when the user has moved the caret out of it.
        TF_SELECTION sel = {};
        ULONG fetched = 0;
        if (FAILED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched != 1) return S_OK;
        ComPtr<ITfRange> selection;
        selection.Attach(sel.range);
        ComPtr<ITfRange> range;
        if (FAILED(composition_->GetRange(&range))) return S_OK;
        LONG start = 0, end = 0;
        selection->CompareStart(ec, range.Get(), TF_ANCHOR_START, &start);
        selection->CompareEnd(ec, range.Get(), TF_ANCHOR_END, &end);
        if (start < 0 || end > 0) commit_composition(TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition* composition) {
    try {
        // The application has ended the composition; the text stays as it is.
        composition_.Reset();
        composition_context_.Reset();
        if (engine_) run_without_document([this] { engine_->composition_terminated(); });
        if (candidate_window_) candidate_window_->hide();
        table_visible_ = false;
    } catch (...) {
    }
    (void)composition;
    return S_OK;
}

ComPtr<ITfContext> TextService::focused_context() {
    ComPtr<ITfContext> context;
    ComPtr<ITfDocumentMgr> focus;
    if (thread_mgr_ && SUCCEEDED(thread_mgr_->GetFocus(&focus)) && focus) focus->GetTop(&context);
    return context;
}

void TextService::change_mode(Mode mode) {
    if (!engine_ || engine_->get_mode() == mode) return;
    if (direct::DirectClient* client = direct_client()) {
        run_direct(client, [this, mode] { engine_->set_mode(mode, true); });
        return;
    }
    ComPtr<ITfContext> context = composition_context_ ? composition_context_ : focused_context();
    if (engine_->has_pending_input() && context) {
        HRESULT hr = run_session(context.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                                 [this, mode](SessionClient&) { engine_->set_mode(mode, true); });
        if (SUCCEEDED(hr) && hr != TF_S_ASYNC) return;
    }
    run_without_document([this, mode] { engine_->set_mode(mode, true); });
}

void TextService::on_mode_changed(Mode mode) {
    LOG_DEBUG("mode changed: %s", to_utf8(ustring(1, mode)).c_str());
    if (mode == MODE_ALPHANUMERIC) {
        table_visible_ = false;
        if (candidate_window_) candidate_window_->hide();
    }
    sync_compartments(mode);
    if (direct_hwnd_ && IsWindow(direct_hwnd_)) {
        // An IMM32 window connected directly: keep the open status of its
        // input context, or the keys stop reaching the text service.
        if (HIMC himc = ImmGetContext(direct_hwnd_)) {
            bool open = mode != MODE_ALPHANUMERIC;
            if ((ImmGetOpenStatus(himc) != FALSE) != open) {
                updating_compartment_ = true;
                ImmSetOpenStatus(himc, open ? TRUE : FALSE);
                updating_compartment_ = false;
            }
            ImmReleaseContext(direct_hwnd_, himc);
        }
    }
    if (langbar_) langbar_->update();
}

void TextService::sync_compartments(Mode mode) {
    if (!thread_mgr_ || updating_compartment_) return;
    ComPtr<ITfCompartmentMgr> compartments;
    if (FAILED(thread_mgr_.As(&compartments))) return;
    updating_compartment_ = true;
    const std::pair<const GUID*, LONG> values[] = {
        {&GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, mode != MODE_ALPHANUMERIC ? 1 : 0},
        {&GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, static_cast<LONG>(conversion_mode_of(mode))},
    };
    for (const auto& [guid, value] : values) {
        ComPtr<ITfCompartment> c;
        if (SUCCEEDED(compartments->GetCompartment(*guid, &c))) {
            VARIANT current;
            VariantInit(&current);
            if (SUCCEEDED(c->GetValue(&current)) && current.vt == VT_I4 && current.lVal == value) continue;
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = value;
            c->SetValue(client_id_, &v);
        }
    }
    updating_compartment_ = false;
}

STDMETHODIMP TextService::OnChange(REFGUID guid) {
    try {
        if (updating_compartment_ || !engine_ || !thread_mgr_) return S_OK;
        ComPtr<ITfCompartmentMgr> compartments;
        if (FAILED(thread_mgr_.As(&compartments))) return S_OK;
        auto read = [&](const GUID& g, LONG& out) {
            ComPtr<ITfCompartment> c;
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(compartments->GetCompartment(g, &c)) && SUCCEEDED(c->GetValue(&v)) && v.vt == VT_I4) {
                out = v.lVal;
                return true;
            }
            return false;
        };
        LONG open = 0;
        LONG conversion = 0;
        Mode mode = engine_->get_mode();
        if (guid == GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) {
            if (!read(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, open)) return S_OK;
            if (open && mode == MODE_ALPHANUMERIC) {
                Mode wanted = MODE_HIRAGANA;
                if (read(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, conversion)) {
                    Mode m = mode_of_conversion(static_cast<DWORD>(conversion));
                    if (m != MODE_ALPHANUMERIC) wanted = m;
                }
                change_mode(wanted);
            } else if (!open && mode != MODE_ALPHANUMERIC) {
                change_mode(MODE_ALPHANUMERIC);
            }
        } else if (guid == GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) {
            if (!read(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, conversion)) return S_OK;
            read(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, open);
            Mode m = mode_of_conversion(static_cast<DWORD>(conversion));
            if (open || m != MODE_ALPHANUMERIC) change_mode(m);
        }
    } catch (...) {
    }
    return S_OK;
}

void TextService::on_candidate_clicked(size_t index) {
    if (!engine_) return;
    if (direct::DirectClient* client = direct_client()) {
        run_direct(client, [this, index] { engine_->candidate_clicked(index); });
        return;
    }
    if (!composition_context_) return;
    ComPtr<ITfContext> context = composition_context_;
    run_session(context.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                [this, index](SessionClient&) { engine_->candidate_clicked(index); });
}

void TextService::on_candidate_wheel(int direction) {
    if (!engine_) return;
    if (direct::DirectClient* client = direct_client()) {
        run_direct(client, [this, direction] {
            if (direction > 0)
                engine_->cursor_down();
            else
                engine_->cursor_up();
        });
        return;
    }
    if (!composition_context_) return;
    ComPtr<ITfContext> context = composition_context_;
    run_session(context.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, [this, direction](SessionClient&) {
        if (direction > 0)
            engine_->cursor_down();
        else
            engine_->cursor_up();
    });
}

void TextService::show_menu(POINT pt) {
    if (!engine_) return;
    enum : UINT {
        ID_HIRAGANA = 1,
        ID_KATAKANA,
        ID_HALFWIDTH_KATAKANA,
        ID_WIDE_ALPHANUMERIC,
        ID_ALPHANUMERIC,
        ID_SETUP = 10,
        ID_USER_DICTIONARY,
        ID_HELP,
        ID_ABOUT,
    };
    const std::pair<UINT, Mode> modes[] = {
        {ID_HIRAGANA, MODE_HIRAGANA},
        {ID_KATAKANA, MODE_KATAKANA},
        {ID_HALFWIDTH_KATAKANA, MODE_HALFWIDTH_KATAKANA},
        {ID_WIDE_ALPHANUMERIC, MODE_WIDE_ALPHANUMERIC},
        {ID_ALPHANUMERIC, MODE_ALPHANUMERIC},
    };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_HIRAGANA, L"ひらがな (あ)");
    AppendMenuW(menu, MF_STRING, ID_KATAKANA, L"カタカナ (ア)");
    AppendMenuW(menu, MF_STRING, ID_HALFWIDTH_KATAKANA, L"半角カタカナ (ｱ)");
    AppendMenuW(menu, MF_STRING, ID_WIDE_ALPHANUMERIC, L"全角英数 (Ａ)");
    AppendMenuW(menu, MF_STRING, ID_ALPHANUMERIC, L"英数 (A)");
    for (const auto& [id, mode] : modes) {
        if (mode == engine_->get_mode()) CheckMenuRadioItem(menu, ID_HIRAGANA, ID_ALPHANUMERIC, id, MF_BYCOMMAND);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_SETUP, L"設定(&S)...");
    AppendMenuW(menu, MF_STRING, ID_USER_DICTIONARY, L"ユーザー辞書を編集(&D)...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_HELP, L"ヘルプ(&H)");
    AppendMenuW(menu, MF_STRING, ID_ABOUT, L"ひらがなIMEについて(&A)");

    if (!menu_owner_) {
        menu_owner_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                      g_module, nullptr);
    }
    SetForegroundWindow(menu_owner_);
    UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y,
                                menu_owner_, nullptr);
    PostMessageW(menu_owner_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    for (const auto& [id, mode] : modes) {
        if (cmd == id) change_mode(mode);
    }
    switch (cmd) {
        case ID_SETUP: open_setup(); break;
        case ID_USER_DICTIONARY: open_user_dictionary(); break;
        case ID_HELP: open_help(); break;
        case ID_ABOUT: {
            std::wstring text = L"ひらがなIME for Windows\n"
                                L"バージョン " HIRAGANA_VERSION_W L"\n\n"
                                L"ibus-hiragana (Copyright 2017-2026 Esrille Inc.) を Windows に移植したものです。\n"
                                L"Apache License 2.0\n"
                                L"辞書: SKK-JISYO (GPLv2), EDICT2 (CC BY-SA 3.0)";
            MessageBoxW(nullptr, text.c_str(), kDescription, MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            break;
        }
        default: break;
    }
}

void TextService::open_setup() {
    fs::path exe = module_dir() / L"hiragana-setup.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void TextService::open_user_dictionary() {
    fs::path path = user_dictionary_path(settings_);
    if (path.empty()) path = user_data_dir() / L"my.dic";
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        f << user_dictionary_template();
    }
    std::wstring args = L"\"" + path.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void TextService::open_help() {
    fs::path index = module_dir() / L"docs" / L"index.html";
    std::error_code ec;
    if (fs::exists(index, ec))
        ShellExecuteW(nullptr, L"open", index.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else
        ShellExecuteW(nullptr, L"open", L"https://esrille.github.io/ibus-hiragana/", nullptr, nullptr, SW_SHOWNORMAL);
}

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** e) {
    return enum_display_attribute_info(e);
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** info) {
    return get_display_attribute_info(guid, info);
}

}  // namespace hiragana::tsf
