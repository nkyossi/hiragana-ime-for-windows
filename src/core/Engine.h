// Hiragana IME for Windows
//
// Copyright (c) 2017-2026 Esrille Inc. (ibus-hiragana)
// Windows port: Copyright (c) 2026 Hiragana IME for Windows contributors
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

// Port of EngineModeless and EngineHiragana in engine/engine.py without the
// LLM-assisted conversion.
//
// The IBus.Engine methods the original uses are provided by a Client:
//   get_surrounding_text()    -> Client::get_surrounding_text()
//   delete_surrounding_text() -> Client::delete_surrounding_text()
//   commit_text()             -> Client::commit_text()
//   update_preedit_text()     -> Client::update_preedit()
//   update_lookup_table()     -> Client::update_lookup_table()
//
// Unlike IBus, the text service knows synchronously whether the document
// supports reading the text around the cursor, so the SURROUNDING_COMMITTED
// and SURROUNDING_BROKEN states of the original are not needed.

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Dictionary.h"
#include "KeyboardController.h"
#include "LookupTable.h"
#include "Model.h"
#include "Settings.h"
#include "Text.h"

namespace hiragana {

struct PreeditSegment {
    enum Style {
        Input,      // underlined text being typed (roman letters, legacy-mode text)
        Candidate,  // the current conversion candidate
        Locked,     // the prefix lock indicator of New Stickney
    };
    ustring text;
    Style style;
};

struct Preedit {
    std::vector<PreeditSegment> segments;

    ustring text() const {
        ustring s;
        for (const auto& seg : segments) s += seg.text;
        return s;
    }
    bool empty() const { return text().empty(); }
};

class Client {
public:
    virtual ~Client() = default;
    // The text before the cursor, excluding the preedit text; pos is the cursor position.
    virtual void get_surrounding_text(ustring& text, index_t& pos) = 0;
    // Deletes size characters before the cursor.
    virtual void delete_surrounding_text(index_t size) = 0;
    virtual void commit_text(const ustring& text) = 0;
    virtual void update_preedit(const Preedit& preedit) = 0;
    virtual void update_lookup_table(const LookupTable& table, bool visible) = 0;
    // Called when the input mode has changed.
    virtual void mode_changed(Mode mode) {}
};

class Engine : public KeyHandler {
public:
    struct Paths {
        std::filesystem::path system_dir;  // holds dic/ and layouts/
        std::filesystem::path user_dir;    // holds my.dic, dic/ and layouts/ of the user
    };

    Engine(const Paths& paths, const Settings& settings, const std::string& xkb);
    ~Engine() override;

    // The client used by the following calls.
    void set_client(Client* client) { client_ = client; }
    // The language model used for the LLM-assisted conversion; null disables
    // the feature (the original loads it from the use-llm setting).
    void set_model(std::shared_ptr<Model> model) { model_ = std::move(model); }
    const std::shared_ptr<Model>& model() const { return model_; }
    // False when the document cannot report the text before the cursor.
    // The engine then keeps everything in the preedit until it is committed.
    void set_surrounding_supported(bool supported) { surrounding_supported_ = supported; }
    bool surrounding_supported() const { return surrounding_supported_; }

    // do_process_key_event()
    bool process_key_event(keysym::Keysym keyval, uint32_t keycode, uint32_t state);
    // True if a key press cannot change anything, so the text service can let
    // it go without opening an edit session. See note_trivial_key().
    bool is_trivial_key(keysym::Keysym keyval, uint32_t keycode, uint32_t state) const;
    // Keeps track of a key press that is_trivial_key() let go.
    void note_trivial_key(keysym::Keysym keyval, uint32_t keycode, uint32_t state);

    void enable(std::optional<bool> caps_lock);
    void disable();
    void focus_in();
    void focus_out();
    // do_reset()
    void reset();
    // The document has ended the composition by itself; forget the conversion.
    void composition_terminated();
    // Commits everything pending, e.g. before the focus moves.
    void commit_all();
    // Applies changed settings; reloads the dictionary or the layout if needed.
    void apply_settings(const Settings& settings, const std::string& xkb);
    // Picks up the input history saved by other processes.
    void sync_dictionary();
    // Reloads the dictionaries, e.g. after the user dictionary has been edited.
    void reload_dictionary();

    // KeyHandler
    Mode get_mode() const override { return mode_; }
    void set_mode(Mode mode, bool override) override;
    bool is_lookup_table_visible() const override { return 0 < lookup_table_.get_number_of_candidates(); }
    bool process_key_event(const Event& e) override;

    void set_mode_by_caps_lock(bool lock);
    bool is_onoff_by_caps() const { return controller_->is_onoff_by_caps(); }
    bool is_enabled() const { return get_mode() != MODE_ALPHANUMERIC; }

    // Candidate window operations
    void candidate_clicked(size_t index_in_page);
    bool page_up();
    bool page_down();
    bool cursor_up();
    bool cursor_down();

    // True while a candidate, roman letters, katakana or legacy-mode text is pending.
    bool has_pending_input() const;
    const LookupTable& lookup_table() const { return lookup_table_; }
    const Settings& settings() const { return settings_; }
    Dictionary& dictionary() { return *dict_; }

private:
    // EngineModeless
    bool backspace();
    void clear();
    ustring clear_preedit();
    ustring clear_roman();
    ustring commit_roman();
    ustring clear_katakana();
    ustring commit_katakana();
    ustring commit_string(const ustring& text);
    void commit_n();
    void delete_surrounding_string(index_t size);
    ustring flush(const ustring& text = ustring(), bool force = false);
    void get_surrounding_string(ustring& text, index_t& pos);
    void get_surrounding_string() {
        ustring t;
        index_t p;
        get_surrounding_string(t, p);
    }
    bool has_preedit() const { return preedit_text_.has_value(); }
    bool has_non_empty_preedit() const { return has_preedit() && !preedit_text_->empty(); }
    bool should_draw_preedit() const { return !surrounding_supported_; }

    // EngineHiragana
    ustring confirm_candidate();
    void handle_kana(const ustring& preedit, const ustring& c, const Event& e, ustring& yomi, ustring& out_preedit);
    std::unique_ptr<Dictionary> load_dictionary(bool clear_history = false);
    void load_layout(const std::string& xkb);
    index_t lookup_dictionary(const ustring& text, index_t pos, index_t anchor, ustring& cand);
    index_t plain_lookup_dictionary(const ustring& text, index_t pos, index_t anchor, ustring& cand);
    void create_lookup_table();
    ustring process_dakuten(const ustring& c);
    void process_escape();
    bool process_expand();
    bool process_katakana();
    bool process_okurigana(index_t pos_yougen);
    bool process_replace(const Event& e);
    bool process_shrink();
    bool process_surrounding_text(const Event& e);
    void reset_state(bool full = true);
    void set_completed(const ustring& cand);
    void update_candidate();
    void update_lookup_table();
    void update_preedit(const ustring& locked = ustring());

    Client* client_ = nullptr;
    Paths paths_;
    Settings settings_;
    std::string xkb_;

    // EngineModeless
    bool surrounding_supported_ = true;
    std::optional<ustring> preedit_text_;
    index_t preedit_pos_ = 0;
    index_t preedit_pos_orig_ = 0;
    index_t preedit_pos_min_ = 0;
    ustring roman_text_;
    ustring katakana_text_;

    // EngineHiragana
    Mode mode_ = MODE_ALPHANUMERIC;
    bool override_ = false;
    std::optional<bool> caps_lock_state_;
    enum class KanaHandler { Default, Kana, Roomazi } to_kana_ = KanaHandler::Default;
    ustring to_tiny_;
    std::vector<ustring> shrunk_;
    // LLM-assisted conversion (engine.py _model, _assisted and _ignored).
    std::shared_ptr<Model> model_;
    index_t assisted_ = 0;
    std::map<ustring, std::set<ustring>> ignored_;
    ustring completed_;
    LookupTable lookup_table_{10};
    bool selected_ = false;  // true if a candidate is selected by the user
    index_t cursor_pos_ = -1;
    ustring character_after_n_;
    bool combining_circumflex_ = false;
    bool combining_macron_ = false;
    bool use_half_width_digits_ = false;

    std::unique_ptr<Dictionary> dict_;
    std::unique_ptr<KeyboardController> controller_;
};

}  // namespace hiragana
