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

#pragma once

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include "DirectClient.h"
#include "DisplayAttributes.h"
#include "Engine.h"
#include "KeyMapping.h"
#include "RemoteModel.h"
#include "Settings.h"

namespace hiragana::tsf {

class CandidateWindow;
class LangBarButton;

class TextService : public ITfTextInputProcessorEx,
                    public ITfThreadMgrEventSink,
                    public ITfThreadFocusSink,
                    public ITfTextEditSink,
                    public ITfKeyEventSink,
                    public ITfCompositionSink,
                    public ITfCompartmentEventSink,
                    public ITfDisplayAttributeProvider {
public:
    TextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor(Ex)
    STDMETHODIMP Activate(ITfThreadMgr* thread_mgr, TfClientId client_id) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id, DWORD flags) override;
    STDMETHODIMP Deactivate() override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* previous) override;
    STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus() override;
    STDMETHODIMP OnKillThreadFocus() override;

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext* context, TfEditCookie ec, ITfEditRecord* record) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override {
        if (eaten) *eaten = FALSE;
        return S_OK;
    }

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID guid) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** e) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** info) override;

private:
    class SessionClient;
    friend class SessionClient;
    class DirectAdapter;
    friend class DirectAdapter;

    // The callbacks of an editor connected directly (see DirectClient.h).
    struct Host final : direct::DirectHost {
        TextService* service = nullptr;
        void __stdcall Reset(direct::DirectClient* client) override;
        void __stdcall CommitAll(direct::DirectClient* client) override;
    };

    ~TextService();

    // Runs fn in an edit session of context with the engine connected to it.
    HRESULT run_session(ITfContext* context, DWORD flags, const std::function<void(SessionClient&)>& fn);
    // Runs fn without a document, e.g. when the mode is changed from the taskbar.
    void run_without_document(const std::function<void()>& fn);
    void finish_session(SessionClient& client);
    // Runs fn with the engine connected to an editor through DirectClient.
    void run_direct(direct::DirectClient* client, const std::function<void()>& fn);
    // Commits what is pending in the editor connected directly and disconnects it.
    void leave_direct();
    BOOL handle_direct_key(HWND hwnd, direct::DirectClient* client, WPARAM wparam, LPARAM lparam, bool release);
    direct::DirectClient* direct_client();

    bool context_accepts_input(ITfContext* context);
    BOOL handle_key(ITfContext* context, WPARAM wparam, LPARAM lparam, bool release);

    void commit_composition(DWORD flags);
    void end_composition(TfEditCookie ec);
    Microsoft::WRL::ComPtr<ITfContext> focused_context();

    void create_engine();
    void refresh_settings(bool force = false);
    // Gives the engine the language model if use-llm is on and the server is ready.
    void update_model();
    std::string xkb() const;

    void change_mode(Mode mode);
    void on_mode_changed(Mode mode);
    void sync_compartments(Mode mode);
    void show_menu(POINT pt);
    void open_setup();
    void open_user_dictionary();
    void open_help();

    void advise_text_edit_sink(ITfContext* context);
    void unadvise_text_edit_sink();

    void on_candidate_clicked(size_t index);
    void on_candidate_wheel(int direction);
    void store_lookup_table(const LookupTable& table, bool visible);
    void show_candidates(const RECT* rect, HWND owner);

    LONG ref_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    DWORD thread_mgr_cookie_ = TF_INVALID_COOKIE;
    DWORD thread_focus_cookie_ = TF_INVALID_COOKIE;
    DWORD openclose_cookie_ = TF_INVALID_COOKIE;
    DWORD conversion_cookie_ = TF_INVALID_COOKIE;
    Microsoft::WRL::ComPtr<ITfContext> text_edit_context_;
    DWORD text_edit_cookie_ = TF_INVALID_COOKIE;
    bool keystroke_advised_ = false;

    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    Microsoft::WRL::ComPtr<ITfContext> composition_context_;
    TfGuidAtom atoms_[static_cast<size_t>(Attribute::Count)] = {};

    std::unique_ptr<Engine> engine_;
    std::shared_ptr<RemoteModel> remote_model_;
    std::unique_ptr<CandidateWindow> candidate_window_;
    LangBarButton* langbar_ = nullptr;
    HWND menu_owner_ = nullptr;
    Host host_;
    HWND direct_hwnd_ = nullptr;  // the editor the engine is connected to directly

    // A key already processed in OnTestKeyDown/OnTestKeyUp. In IMM32 windows
    // (through CUAS), a key the text service has claimed in the test is
    // replaced by VK_PROCESSKEY and does not reach the window even if
    // OnKeyDown leaves it, so the test has to give the real answer.
    struct TestedKey {
        bool valid = false;
        WPARAM wparam = 0;
        LPARAM lparam = 0;
        bool release = false;
        BOOL eaten = FALSE;
    } tested_key_;
    BOOL test_key(ITfContext* context, WPARAM wparam, LPARAM lparam, bool release);
    // Applies the key-swaps setting to k; returns true if the key was swapped.
    bool swap_key(WPARAM wparam, bool release, KeyEvent& k);
    std::set<WPARAM> swapped_keys_;  // swapped keys that are down
    BOOL process_key(ITfContext* context, WPARAM wparam, LPARAM lparam, bool release);

    Settings settings_;
    std::filesystem::file_time_type settings_time_{};
    std::filesystem::file_time_type user_dictionary_time_{};
    DWORD last_settings_check_ = 0;
    bool updating_compartment_ = false;

    // The candidate window state reported by the engine in the last session.
    bool table_visible_ = false;
    std::vector<std::wstring> table_page_;
    size_t table_selected_ = 0;
    size_t table_page_index_ = 0;
    size_t table_page_count_ = 1;
    bool table_changed_ = false;
};

}  // namespace hiragana::tsf
