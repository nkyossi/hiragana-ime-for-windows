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

#include "Engine.h"

#include <cassert>

#include "KanaTables.h"
#include "Log.h"

namespace hiragana {

using namespace keysym;
using namespace kana;

namespace {

constexpr char32_t IAA = 0xFFF9;  // INTERLINEAR ANNOTATION ANCHOR
constexpr char32_t IAS = 0xFFFA;  // INTERLINEAR ANNOTATION SEPARATOR
constexpr char32_t IAT = 0xFFFB;  // INTERLINEAR ANNOTATION TERMINATOR

const ustring kDash(1, DASH);

ustring get_plain_text(const ustring& text) {
    ustring plain;
    bool in_ruby = false;
    for (char32_t c : text) {
        if (c == IAA) {
            in_ruby = false;
        } else if (c == IAS) {
            in_ruby = true;
        } else if (c == IAT) {
            in_ruby = false;
        } else if (!in_ruby) {
            plain += c;
        }
    }
    return plain;
}

ustring remove_dash(const ustring& s) { return replace_all(s, kDash, U""); }

// Python's `sub in s` for a string sub.
bool in(ustring_view s, const ustring& sub) { return s.find(sub) != ustring_view::npos; }

ustring lower(const ustring& s) {
    ustring out;
    for (char32_t c : s) out += to_lower(c);
    return out;
}

bool all_katakana(const ustring& s, size_t from) {
    for (size_t i = from; i < s.size(); ++i) {
        if (!is_katakana(s[i])) return false;
    }
    return true;
}

// RE_TO_KATAKANA = ([HIRAGANA]?)([KATAKANA]*)$ searched in s.
// Returns the start of the match and sets group1 and group2.
index_t search_to_katakana(const ustring& s, ustring& group1, ustring& group2) {
    size_t n = s.size();
    for (size_t i = 0; i <= n; ++i) {
        if (i < n && is_hiragana(s[i]) && all_katakana(s, i + 1)) {
            group1 = s.substr(i, 1);
            group2 = s.substr(i + 1);
            return static_cast<index_t>(i);
        }
        if (all_katakana(s, i)) {
            group1.clear();
            group2 = s.substr(i);
            return static_cast<index_t>(i);
        }
    }
    group1.clear();
    group2.clear();
    return static_cast<index_t>(n);
}

Mode mode_from_string(const std::string& s) {
    ustring u = from_utf8(s);
    if (u.size() == 1 && is_valid_mode(u[0])) return u[0];
    return MODE_ALPHANUMERIC;
}

}  // namespace

Engine::Engine(const Paths& paths, const Settings& settings, const std::string& xkb)
    : paths_(paths), settings_(settings), xkb_(xkb) {
    log::set_level(log::level_from_name(settings_.logging_level));
    dict_ = load_dictionary();
    load_layout(xkb);
    mode_ = mode_from_string(settings_.mode);
    character_after_n_ = settings_.nn_as_jis_x_4063 ? U"aiueo'wyn" : U"aiueo'wy";
    combining_circumflex_ = settings_.combining_circumflex;
    combining_macron_ = settings_.combining_macron;
    use_half_width_digits_ = settings_.use_half_width_digits;
}

Engine::~Engine() {
    try {
        if (dict_) dict_->save_orders();
    } catch (...) {
    }
}

std::unique_ptr<Dictionary> Engine::load_dictionary(bool clear_history) {
    Dictionary::Config config;
    config.system_dir = paths_.system_dir / "dic";
    config.user_dir = paths_.user_dir;
    std::string system = settings_.dictionary;
    size_t slash = system.find_last_of("/\\");
    if (slash != std::string::npos) {
        // for v0.15.0 or earlier
        system = system.substr(slash + 1);
        if (system == "restrained.dic") system = "restrained.8.dic";
    }
    config.system = system;
    config.user = settings_.user_dictionary;
    config.clear_history = clear_history;
    config.permissible = settings_.permissible;
    auto dict = std::make_unique<Dictionary>(config);
    dict->use_romazi(to_kana_ != KanaHandler::Kana);
    return dict;
}

void Engine::load_layout(const std::string& xkb) {
    xkb_ = xkb;
    Layout layout = hiragana::load_layout({paths_.user_dir, paths_.system_dir}, settings_.layout, settings_.altgr, xkb);
    if (layout.is_kana) {
        to_kana_ = KanaHandler::Kana;
        dict_->use_romazi(false);
    } else if (layout.has_roomazi) {
        to_kana_ = KanaHandler::Roomazi;
        dict_->use_romazi(true);
    } else {
        to_kana_ = KanaHandler::Default;
        dict_->use_romazi(true);
    }
    to_tiny_ = layout.tiny;
    controller_ = std::make_unique<KeyboardController>(std::move(layout));
}

void Engine::apply_settings(const Settings& settings, const std::string& xkb) {
    Settings old = settings_;
    settings_ = settings;
    log::set_level(log::level_from_name(settings_.logging_level));
    if (!old.same_layout(settings) || xkb != xkb_) {
        reset_state();
        load_layout(xkb);
    }
    if (!old.same_dictionary(settings)) {
        reset_state();
        // Do not write the old history over the one the setup program has cleared.
        if (old.history_generation == settings.history_generation) dict_->save_orders();
        else ignored_.clear();
        dict_ = load_dictionary();
    }
    if (old.mode != settings.mode) {
        Mode mode = mode_from_string(settings.mode);
        if (mode != get_mode()) set_mode(mode, true);
    }
    character_after_n_ = settings_.nn_as_jis_x_4063 ? U"aiueo'wyn" : U"aiueo'wy";
    combining_circumflex_ = settings_.combining_circumflex;
    combining_macron_ = settings_.combining_macron;
    use_half_width_digits_ = settings_.use_half_width_digits;
}

void Engine::reload_dictionary() {
    reset_state();
    dict_->save_orders();
    dict_ = load_dictionary();
}

void Engine::sync_dictionary() {
    if (!dict_->current().empty()) return;
    dict_->sync();
}

//
// EngineModeless
//
bool Engine::backspace() {
    if (!roman_text_.empty()) {
        roman_text_.pop_back();
        return true;
    }
    if (should_draw_preedit() && has_preedit() && 0 < preedit_pos_) {
        ustring& t = *preedit_text_;
        t = slice_to(t, preedit_pos_ - 1) + slice_from(t, preedit_pos_);
        --preedit_pos_;
        return true;
    }
    return false;
}

void Engine::clear() {
    preedit_text_.reset();
    preedit_pos_ = 0;
    roman_text_.clear();
    katakana_text_.clear();
}

ustring Engine::clear_preedit() {
    ustring text = preedit_text_.value_or(ustring());
    preedit_text_ = ustring();
    preedit_pos_ = 0;
    return text;
}

ustring Engine::clear_roman() {
    ustring text = roman_text_;
    roman_text_.clear();
    return text;
}

ustring Engine::commit_roman() {
    ustring text = roman_text_;
    if (!text.empty()) {
        if (text == U"n") text = U"ん";
        commit_string(text);
        roman_text_.clear();
    }
    return text;
}

ustring Engine::clear_katakana() {
    ustring text = katakana_text_;
    katakana_text_.clear();
    return text;
}

ustring Engine::commit_katakana() {
    // Note the katakana part has been committed in get_surrounding_string().
    ustring text = katakana_text_;
    if (!text.empty()) katakana_text_.clear();
    return text;
}

ustring Engine::commit_string(const ustring& text) {
    LOG_DEBUG("commit_string(\"%s\")", to_utf8(text).c_str());
    if (text.empty()) return text;
    if (!has_preedit()) get_surrounding_string();
    ustring& t = *preedit_text_;
    t = slice_to(t, preedit_pos_) + text + slice_from(t, preedit_pos_);
    preedit_pos_ += len(text);
    return text;
}

void Engine::commit_n() {
    clear_roman();
    commit_string(U"ん");
    if (!should_draw_preedit() && client_) {
        client_->commit_text(U"ん");
    }
    preedit_pos_min_ += 1;
    preedit_pos_orig_ += 1;
}

void Engine::delete_surrounding_string(index_t size) {
    LOG_DEBUG("delete_surrounding_string(%td)", size);
    if (!has_preedit()) get_surrounding_string();
    if (size > preedit_pos_) size = preedit_pos_;
    if (size <= 0) return;
    ustring& t = *preedit_text_;
    t = slice_to(t, preedit_pos_ - size) + slice_from(t, preedit_pos_);
    preedit_pos_ -= size;
    if (preedit_pos_ < preedit_pos_min_) preedit_pos_min_ = preedit_pos_;
}

// Note roman_text_ is not flushed; use commit_roman() first.
ustring Engine::flush(const ustring& text, bool force) {
    LOG_DEBUG("flush(\"%s\", %d)", to_utf8(text).c_str(), force);
    if (!text.empty()) commit_string(text);
    if (should_draw_preedit()) {
        if (has_non_empty_preedit() && client_) client_->commit_text(*preedit_text_);
    } else if (has_preedit()) {
        index_t delete_size = preedit_pos_orig_ - preedit_pos_min_;
        if (0 < delete_size && client_) client_->delete_surrounding_text(delete_size);
        if (preedit_pos_min_ < preedit_pos_ && client_) {
            client_->commit_text(slice(*preedit_text_, preedit_pos_min_, preedit_pos_));
        }
    }
    ustring result = preedit_text_.value_or(ustring());
    preedit_text_.reset();
    preedit_pos_ = 0;
    preedit_pos_min_ = 0;
    preedit_pos_orig_ = 0;
    return result;
}

void Engine::get_surrounding_string(ustring& text, index_t& pos) {
    // NOTE: Keep katakana text in preedit during conversion
    if (should_draw_preedit()) {
        if (!has_preedit()) clear_preedit();
        if (!katakana_text_.empty()) {
            ustring& t = *preedit_text_;
            t = slice_to(t, preedit_pos_) + katakana_text_ + slice_from(t, preedit_pos_);
            preedit_pos_ += len(katakana_text_);
            katakana_text_.clear();
        }
        text = *preedit_text_;
        pos = preedit_pos_;
        return;
    }

    if (has_preedit()) {
        text = *preedit_text_;
        pos = preedit_pos_;
        return;
    }

    // Cache the current surrounding text in preedit_text_ and preedit_pos_.
    // The text service excludes the preedit text from the surrounding text, so
    // the workarounds for Kate and LibreOffice in engine.py are not needed.
    text.clear();
    pos = 0;
    if (client_) client_->get_surrounding_text(text, pos);
    if (pos < 0 || len(text) < pos) pos = len(text);
    if (!katakana_text_.empty()) {
        if (client_) client_->commit_text(katakana_text_);
        text = slice_to(text, pos) + katakana_text_ + slice_from(text, pos);
        pos += len(katakana_text_);
    }
    preedit_text_ = text;
    preedit_pos_ = pos;
    preedit_pos_min_ = pos;
    preedit_pos_orig_ = pos;
    LOG_DEBUG("get_surrounding_string: \"%s\", %td", to_utf8(text).c_str(), pos);
}

//
// EngineHiragana
//
ustring Engine::confirm_candidate() {
    ustring current = dict_->current();
    if (!current.empty()) {
        if (!dict_->is_pseudo_candidate()) {
            auto stem = dict_->get_stem(assisted_);
            const ustring& yomi = stem.first;
            const ustring& assisted = stem.second;
            ustring shrunk;
            for (const auto& s : shrunk_) shrunk += s;
            index_t no = dict_->confirm(shrunk);
            if (no == assisted_) {
                auto found = ignored_.find(yomi);
                if (found != ignored_.end()) found->second.erase(assisted);
            } else if (0 <= assisted_ && assisted_ < static_cast<index_t>(dict_->cand().size())) {
                ignored_[yomi].insert(assisted);
                if (dict_->is_rejected(yomi, assisted)) dict_->remove_entry(yomi, assisted);
            }
        }
        dict_->reset();
        lookup_table_.clear();
    }
    return current;
}

void Engine::handle_kana(const ustring& preedit_in, const ustring& c_in, const Event& e, ustring& yomi_out,
                         ustring& preedit_out) {
    switch (to_kana_) {
        case KanaHandler::Default:
            yomi_out = c_in;
            preedit_out.clear();
            return;
        case KanaHandler::Kana: {
            ustring k = controller_->kana(e);
            yomi_out = !k.empty() ? k : c_in;
            preedit_out = preedit_in;
            return;
        }
        case KanaHandler::Roomazi:
            break;
    }
    ustring yomi;
    ustring post;
    ustring preedit = preedit_in;
    ustring c = lower(c_in);
    if (in(CIRCUMFLEX, c)) {
        c = from_circumflex(c);
        post = U"ー";
    } else if (in(MACRON, c)) {
        c = from_macron(c);
        post = U"ー";
    }
    if (preedit == U"n" && character_after_n_.find(c) == ustring::npos) {
        yomi = U"ん";
        preedit = slice_from(preedit, 1);
    }
    preedit += c;
    ustring k = controller_->transliterate_back(preedit);
    if (!k.empty()) {
        yomi += k + post;
        if (yomi == U"ー") {
            ustring text;
            index_t pos;
            get_surrounding_string(text, pos);
            if (pos <= 0 || !(is_hiragana(text[pos - 1]) || is_katakana(text[pos - 1]))) yomi = U"－";
        }
        preedit.clear();
    } else if (2 <= preedit.size() && preedit[0] == preedit[1] && contains(SOKUON, preedit[1])) {
        yomi += U"っ";
        preedit = slice_from(preedit, 1);
    } else if (preedit == c && !in(U"abcdefghijklmnopqrstuvwxyz", c)) {
        yomi += c;
        preedit.clear();
    }
    yomi_out = yomi;
    preedit_out = preedit;
}

index_t Engine::plain_lookup_dictionary(const ustring& text, index_t pos, index_t anchor, ustring& cand) {
    cand = dict_->lookup(text, pos, anchor);
    index_t size = len(dict_->reading());
    selected_ = false;
    assisted_ = 0;
    // Note if the current conversion is complete, it is automatically confirmed.
    if (0 < size && 1 < dict_->cand().size() && !dict_->is_complete()) cursor_pos_ = 0;
    return size;
}

// engine.py _assisted_lookup_dictionary(); falls back to the plain lookup when
// no language model is set.
index_t Engine::lookup_dictionary(const ustring& text, index_t pos, index_t anchor, ustring& cand) {
    if (!model_) return plain_lookup_dictionary(text, pos, anchor, cand);
    assert(anchor <= pos);
    ustring plain = get_plain_text(slice_to(text, anchor));
    index_t new_anchor = len(plain);
    plain += get_plain_text(slice(text, anchor, pos));
    index_t new_pos = len(plain);
    plain += get_plain_text(slice_from(text, pos));
    anchor = new_anchor;
    pos = new_pos;

    index_t cursor_pos = 0;
    cand = dict_->assisted_lookup(*model_, plain, pos, anchor, cursor_pos);
    index_t size = len(dict_->reading());
    selected_ = false;
    assisted_ = cursor_pos;
    // Note if the current conversion is complete, it is automatically confirmed.
    if (0 < size && 1 < dict_->cand().size() && !dict_->is_complete()) {
        if (0 < cursor_pos) {
            auto stem = dict_->get_stem(cursor_pos);
            auto found = ignored_.find(stem.first);
            if (found != ignored_.end() && found->second.count(stem.second) != 0) {
                cursor_pos = 0;
                LOG_DEBUG("_assisted_lookup_dictionary: ignore \"%s\", use \"%s\"", to_utf8(stem.second).c_str(),
                          to_utf8(dict_->cand()[cursor_pos]).c_str());
            } else {
                dict_->set_current(cursor_pos);
            }
        }
        cursor_pos_ = cursor_pos;
    }
    return size;
}

void Engine::create_lookup_table() {
    index_t cursor_pos = cursor_pos_;
    cursor_pos_ = -1;
    lookup_table_.clear();
    for (const auto& c : dict_->cand()) lookup_table_.append_candidate(c);
    lookup_table_.set_cursor_pos(static_cast<size_t>(cursor_pos));
}

ustring Engine::process_dakuten(const ustring& c_in) {
    ustring c = c_in;
    ustring text;
    index_t pos;
    get_surrounding_string(text, pos);
    if (pos <= 0) return c;
    if (c == U"゛") {
        index_t found = find(NON_DAKU, text[pos - 1]);
        if (0 <= found) {
            delete_surrounding_string(1);
            c = ustring(1, DAKU[found]);
        }
    } else if (c == U"゜") {
        index_t found = find(NON_HANDAKU, text[pos - 1]);
        if (0 <= found) {
            delete_surrounding_string(1);
            c = ustring(1, HANDAKU[found]);
        }
    } else if (!to_tiny_.empty() && roman_text_ == to_tiny_) {
        index_t found = find(NON_TINY, text[pos - 1]);
        if (0 <= found) {
            roman_text_.clear();
            delete_surrounding_string(1);
            c = ustring(1, TINY[found]);
        }
    }
    return c;
}

void Engine::process_escape() {
    clear_roman();
    ustring yomi = dict_->reading();
    reset_state(false);
    commit_string(remove_dash(yomi));
}

bool Engine::process_expand() {
    if (shrunk_.empty()) return true;
    ustring kana = shrunk_.back();
    ustring yomi = dict_->reading();
    ustring text;
    index_t pos;
    get_surrounding_string(text, pos);
    text = slice_to(text, pos) + yomi + slice_from(text, pos);
    pos += len(yomi);
    ustring cand;
    lookup_dictionary(text, pos, pos - len(kana + yomi), cand);
    delete_surrounding_string(len(kana));
    shrunk_.pop_back();
    return true;
}

bool Engine::process_katakana() {
    ustring text;
    index_t pos;
    get_surrounding_string(text, pos);
    ustring ch;
    if (roman_text_ == U"n") {
        clear_roman();
        ch = U"ん";
    }
    ustring s = slice_to(text, pos) + ch;
    ustring group1, group2;
    index_t start = search_to_katakana(s, group1, group2);
    index_t matched = len(s) - start;
    delete_surrounding_string(matched - len(ch));
    katakana_text_ = to_katakana(group1) + group2;
    return true;
}

bool Engine::process_okurigana(index_t pos_yougen) {
    ustring text;
    index_t pos;
    get_surrounding_string(text, pos);
    index_t suffix = rfind(slice(text, pos_yougen, pos), kDash);
    suffix += pos_yougen;
    LOG_DEBUG("_process_okurigana(%td): \"%s\", %td/%td", pos_yougen, to_utf8(slice_to(text, pos)).c_str(),
              suffix, pos);
    ustring cand;
    index_t size = 0;
    if (suffix < pos) size = lookup_dictionary(slice_to(text, pos), pos, pos_yougen, cand);
    if (dict_->current().empty()) {
        cand = slice(text, pos_yougen, pos);
        size = len(cand);
        dict_->create_pseudo_candidate(cand);
    }
    delete_surrounding_string(size);
    return true;
}

bool Engine::process_replace(const Event& e) {
    LOG_DEBUG("_process_replace(): _completed=\"%s\"", to_utf8(completed_).c_str());
    if (!dict_->current().empty()) return true;
    // Check 'n'
    bool committed_n = false;
    if (roman_text_ == U"n") {
        committed_n = true;
        commit_n();
    }
    ustring text;
    index_t pos;
    get_surrounding_string(text, pos);
    ustring cand;
    index_t size = 0;
    // Check Return for yôgen conversion
    if (e.is_henkan() || e.is_key(Return)) {
        size = lookup_dictionary(text, pos, 0, cand);
    } else if (1 <= pos) {
        // 用言として変換する
        index_t suffix = rfind(slice_to(text, pos), kDash);
        if (0 < suffix) {
            size = lookup_dictionary(text, pos, 0, cand);
        } else {
            commit_string(kDash);
            get_surrounding_string(text, pos);
            size = lookup_dictionary(text, pos, 0, cand);
            if (cand.empty()) delete_surrounding_string(1);
        }
    }
    if (!dict_->current().empty()) {
        shrunk_.clear();
        delete_surrounding_string(size);
        if (!completed_.empty()) {
            // 直前の変換のおくりがなの部分をスキップするようする。
            ustring plain = get_plain_text(slice_to(text, pos));
            index_t completed_pos = rfind(plain, completed_);
            if (0 <= completed_pos) {
                index_t max_size = len(plain) - completed_pos - len(completed_);
                while (max_size < size) {
                    process_shrink();
                    ustring current = dict_->current();
                    if (cand == current) break;
                    LOG_DEBUG("auto shrink: from \"%s\" to \"%s\"", to_utf8(cand).c_str(), to_utf8(current).c_str());
                    cand = current;
                    size = len(dict_->reading());
                }
            }
        }
    } else if (!committed_n) {
        // Commit a white space
        commit_string((e.is_muhenkan() || get_mode() == MODE_HALFWIDTH_KATAKANA) ? U" " : U"　");
    }
    return true;
}

bool Engine::process_shrink() {
    LOG_DEBUG("_process_shrink: \"%s\"", to_utf8(dict_->current()).c_str());
    ustring yomi = dict_->reading();
    if (len(yomi) <= 1 || yomi[1] == DASH) return true;
    ustring text;
    index_t pos;
    get_surrounding_string(text, pos);
    text = slice_to(text, pos) + yomi + slice_from(text, pos);
    pos += len(yomi);
    ustring cand;
    index_t size = lookup_dictionary(text, pos, pos - (len(yomi) - 1), cand);
    if (0 < size) {
        ustring kana = slice_to(yomi, len(yomi) - size);
        shrunk_.push_back(kana);
        commit_string(kana);
    } else {
        // Restore dict_
        lookup_dictionary(text, pos, pos - len(yomi), cand);
    }
    return true;
}

bool Engine::process_surrounding_text(const Event& e) {
    if (!dict_->current().empty()) {
        if (e.keyval == Tab) {
            if (!e.is_shift()) return process_shrink();
            return process_expand();
        }
        if (e.keyval == Escape) {
            process_escape();
            return true;
        }
        if (e.keyval == Return) {
            if (dict_->is_pseudo_candidate()) {
                process_escape();
                return true;
            }
            ustring current = confirm_candidate();
            commit_string(current);
            if (last(current) == DASH) return process_replace(e);
            if (commit_roman().empty()) set_completed(current);
            flush();
            return true;
        }
    }

    ustring katakana;
    if (e.keyval == Return) {
        if (!commit_roman().empty()) return true;
        katakana = commit_katakana();
        if (!katakana.empty()) {
            dict_->add_katakana(katakana);
            return true;
        }
    }
    if (e.keyval == Escape) {
        if (!clear_roman().empty()) return true;
        katakana = clear_katakana();
        if (!katakana.empty()) {
            delete_surrounding_string(len(katakana));
            commit_string(to_hiragana(katakana));
            return true;
        }
    }

    if (get_mode() == MODE_HIRAGANA && (e.is_henkan() || e.is_muhenkan()) && !e.has_altgr() &&
        katakana_text_.empty()) {
        return process_replace(e);
    }

    ustring text;
    index_t pos;
    get_surrounding_string(text, pos);
    index_t pos_yougen = -1;
    bool to_revert = false;
    ustring current = dict_->current();
    if (!current.empty()) {
        // Commit the current candidate
        ustring yomi = dict_->reading();
        confirm_candidate();
        commit_string(remove_dash(current));
        LOG_DEBUG("yomi: '%s', current: '%s', roman: '%s'", to_utf8(yomi).c_str(), to_utf8(current).c_str(),
                  to_utf8(roman_text_).c_str());
        if (last(current) == DASH) {
            // yomi: の, current: の―
            // yomi: うご, current: うご―
            pos_yougen = pos;
            to_revert = true;
        } else if (contains(OKURIGANA, last(current)) || !roman_text_.empty()) {
            // yomi: の―た, current: の―た
            // yomi: うご―い, current: 動い
            if (!selected_) {
                pos_yougen = pos;
                to_revert = true;
                current = yomi;
            } else {
                set_completed(current);
                if (should_draw_preedit()) flush();
            }
        } else if (should_draw_preedit()) {
            flush();
        }
    }

    if (e.is_katakana()) {
        process_katakana();
        return true;
    }
    commit_katakana();

    if (e.is_backspace()) {
        if (to_revert && last(current) != DASH) {
            if (!roman_text_.empty()) {
                roman_text_.pop_back();
            } else {
                current = slice_to(current, len(current) - 1);
            }
            get_surrounding_string(text, pos);
            delete_surrounding_string(pos - pos_yougen);
            commit_string(current);
            return process_okurigana(pos_yougen);
        }
        return backspace();
    }

    ustring yomi;
    ustring c = e.get_string();
    if (e.has_altgr()) {
        if (c.empty()) {
            clear_roman();
            return true;
        }
        if (!in(CIRCUMFLEX, c)) yomi = c;
    }
    if (!c.empty()) {
        if (!yomi.empty()) {
            // AltGr character
        } else if (get_mode() == MODE_ALPHANUMERIC) {
            get_surrounding_string(text, pos);
            if (combining_circumflex_ && 0 < pos && c == U"^") {
                if (contains(VOWELS, text[pos - 1])) {
                    yomi = to_circumflex(ustring(1, text[pos - 1]));
                    delete_surrounding_string(1);
                } else if (contains(CIRCUMFLEX, text[pos - 1])) {
                    yomi = from_circumflex(ustring(1, text[pos - 1])) + c;
                    delete_surrounding_string(1);
                } else {
                    return false;
                }
            } else if (combining_macron_ && 0 < pos && c == U"~") {
                if (contains(VOWELS, text[pos - 1])) {
                    yomi = to_macron(ustring(1, text[pos - 1]));
                    delete_surrounding_string(1);
                } else if (contains(MACRON, text[pos - 1])) {
                    yomi = from_macron(ustring(1, text[pos - 1])) + c;
                    delete_surrounding_string(1);
                } else {
                    return false;
                }
            } else {
                if (c.size() == 1 && c[0] == e.keyval) return false;
                yomi = c;
            }
        } else if (get_mode() == MODE_WIDE_ALPHANUMERIC) {
            yomi = to_zenkaku(c);
        } else {
            ustring roman;
            handle_kana(roman_text_, c, e, yomi, roman);
            roman_text_ = roman;
            if (!yomi.empty()) {
                if (get_mode() == MODE_KATAKANA) {
                    yomi = to_katakana(yomi);
                } else if (get_mode() == MODE_HALFWIDTH_KATAKANA) {
                    yomi = to_hankaku(to_katakana(yomi));
                }
                if (use_half_width_digits_) {
                    get_surrounding_string(text, pos);
                    if (0 < pos && contains(HANKAKU_DIGIT, text[pos - 1]))
                        yomi = to_hankaku_digit_extra(yomi);
                    else
                        yomi = to_hankaku_digit(yomi);
                }
            }
        }
    } else if (e.is_prefix()) {
        // New Stickney prefix key
    } else if (has_non_empty_preedit() && should_draw_preedit()) {
        if (e.keyval == Escape) {
            clear_preedit();
        } else {
            clear_roman();
            flush();
        }
        return true;
    } else {
        // Let the application process the key
        clear_roman();
        return false;
    }

    if (to_revert && (yomi.empty() || contains(OKURIGANA, last(yomi)) || !roman_text_.empty())) {
        get_surrounding_string(text, pos);
        delete_surrounding_string(pos - pos_yougen);
        commit_string(current);
    }
    yomi = process_dakuten(yomi);
    commit_string(yomi);
    if (0 <= pos_yougen && (yomi.empty() || contains(OKURIGANA, last(yomi)) || !roman_text_.empty())) {
        process_okurigana(pos_yougen);
        current = dict_->current();
        if (!current.empty() && dict_->is_complete()) {
            confirm_candidate();
            commit_string(current);
            if (is_hiragana(last(current))) set_completed(slice_to(current, len(current) - 1));
        }
    }
    return true;
}

void Engine::reset_state(bool full) {
    dict_->reset();
    lookup_table_.clear();
    update_lookup_table();
    completed_.clear();
    if (full) clear();
    update_preedit();
}

void Engine::set_completed(const ustring& cand) {
    LOG_DEBUG("_set_completed(\"%s\")", to_utf8(cand).c_str());
    if (!cand.empty() && is_hiragana(last(cand)))
        completed_ = cand;
    else
        completed_.clear();
}

void Engine::update_candidate() {
    dict_->set_current(static_cast<index_t>(lookup_table_.get_cursor_pos()));
    update_preedit();
}

void Engine::update_lookup_table() {
    // As in engine.py, the window is left as it is in the alphanumeric mode;
    // the text service hides it when the mode changes.
    if (client_ && is_enabled())
        client_->update_lookup_table(lookup_table_, 0 < lookup_table_.get_number_of_candidates());
}

void Engine::update_preedit(const ustring& locked) {
    ustring preedit_text;
    if (has_non_empty_preedit() && should_draw_preedit()) preedit_text = *preedit_text_;
    ustring cand = dict_->current();
    if (!cand.empty()) {
        if (last(cand) != DASH) {
            size_t n = hiragana_prefix_length(cand);
            preedit_text += cand.substr(0, n);
            cand = cand.substr(n);
        }
    } else {
        cand = katakana_text_;
    }
    Preedit preedit;
    if (!preedit_text.empty()) preedit.segments.push_back({preedit_text, PreeditSegment::Input});
    if (!cand.empty()) preedit.segments.push_back({cand, PreeditSegment::Candidate});
    if (!roman_text_.empty()) preedit.segments.push_back({roman_text_, PreeditSegment::Input});
    if (!locked.empty()) preedit.segments.push_back({locked, PreeditSegment::Locked});
    if (client_) client_->update_preedit(preedit);
    update_lookup_table();
}

//
// public methods
//
void Engine::set_mode_by_caps_lock(bool lock) {
    if (caps_lock_state_ == lock) return;
    LOG_DEBUG("set_mode_by_caps_lock(%d)", lock);
    caps_lock_state_ = lock;
    set_mode(lock ? MODE_HIRAGANA : MODE_ALPHANUMERIC, false);
}

bool Engine::process_key_event(const Event& e) {
    if (e.is_dual_role()) {
        // Dual-role modifiers are processed below.
    } else if (e.is_modifier()) {
        // Ignore modifier keys
        return false;
    } else if (e.state & (CONTROL_MASK | MOD1_MASK)) {
        clear_roman();
        flush(remove_dash(confirm_candidate()));
        update_preedit();
        return false;
    }

    // Handle candidate window
    if (0 < lookup_table_.get_number_of_candidates()) {
        if (e.keyval == Page_Up || e.keyval == KP_Page_Up) return page_up();
        if (e.keyval == Page_Down || e.keyval == KP_Page_Down) return page_down();
        if (e.keyval == Up || e.is_muhenkan()) return cursor_up();
        if (e.keyval == Down || e.is_henkan()) return cursor_down();
    }

    if (e.is_backspace()) {
        if (e.is_prefixed()) {
            // Clear the indication of the prefixed lock.
            update_preedit();
            return true;
        }
        if (!roman_text_.empty()) {
            backspace();
            update_preedit();
            return true;
        }
    }

    // Cache the current surrounding text into the local buffer.
    get_surrounding_string();
    // Edit the local surrounding text buffer as we need.
    bool result = process_surrounding_text(e);
    // Flush the local surrounding text buffer into the document.
    if (get_mode() != MODE_HIRAGANA) {
        flush(ustring(), true);
    } else if (!should_draw_preedit()) {
        flush();
    }

    // Lastly, update the lookup table and preedit text.
    if (0 <= cursor_pos_) create_lookup_table();
    if (e.is_prefix()) {
        update_preedit(e.is_prefixed() ? U"＿" : U"");
    } else {
        update_preedit();
    }
    return result;
}

bool Engine::process_key_event(Keysym keyval, uint32_t keycode, uint32_t state) {
    LOG_DEBUG("do_process_key_event(%s, %u, %#010x)", name(keyval).c_str(), keycode, state);
    if (keyval == Super_L || keyval == Super_R || (state & MOD4_MASK)) return false;
    return controller_->process_key_event(*this, keyval, keycode, state);
}

bool Engine::is_trivial_key(Keysym keyval, uint32_t keycode, uint32_t state) const {
    if (state & RELEASE_MASK) return !controller_->tracks_release(keyval);
    if (keyval == Super_L || keyval == Super_R || (state & MOD4_MASK)) return true;
    if (get_mode() != MODE_ALPHANUMERIC || has_pending_input()) return false;
    if (combining_circumflex_ || combining_macron_) return false;
    if (controller_->tracks_release(keyval)) return false;
    switch (keyval) {
        case Caps_Lock:
        case Muhenkan:
        case Henkan:
        case Hiragana_Katakana:
        case Hangul:
        case Hangul_Hanja:
        case Eisu_toggle:
        case Zenkaku_Hankaku:
        case ISO_Level3_Shift:
        case Meta_L:
        case Meta_R:
            return false;
        default:
            break;
    }
    if (state & (CONTROL_MASK | MOD1_MASK)) return true;
    Keysym k = (keyval == backslash && keycode == 0x7c) ? yen : keyval;
    Event e = controller_->create_event(k, keycode, state);
    if (e.has_altgr()) return false;
    ustring c = e.get_string();
    // engine.py lets the application process the key when chr(keyval) == c.
    return c.empty() || (c.size() == 1 && c[0] == k);
}

void Engine::note_trivial_key(Keysym keyval, uint32_t keycode, uint32_t state) {
    controller_->track_trivial_key(keyval, state);
}

void Engine::set_mode(Mode mode, bool override) {
    LOG_DEBUG("set_mode(%s, %d)", to_utf8(ustring(1, mode)).c_str(), override);
    override_ = override;
    if (mode_ != mode) {
        clear_roman();
        flush(remove_dash(confirm_candidate()));
        update_preedit();
        mode_ = mode;
        update_lookup_table();
        if (client_) client_->mode_changed(mode);
    }
}

void Engine::enable(std::optional<bool> caps_lock) {
    caps_lock_state_.reset();
    if (controller_->is_onoff_by_caps() && caps_lock) set_mode_by_caps_lock(*caps_lock);
}

void Engine::disable() {
    reset_state();
    mode_ = MODE_ALPHANUMERIC;
    dict_->save_orders();
}

void Engine::focus_in() {
    controller_->reset();
    sync_dictionary();
    update_preedit();
}

void Engine::focus_out() {
    reset_state();
    dict_->save_orders();
}

void Engine::reset() {
    if (!dict_->current().empty() || !katakana_text_.empty()) {
        // Firefox 129.0.2 signals an unexpected reset during the conversion.
        // Simply ignore it as the real reset is very rare during the conversion.
        return;
    }
    reset_state();
}

void Engine::composition_terminated() {
    dict_->reset();
    lookup_table_.clear();
    shrunk_.clear();
    completed_.clear();
    cursor_pos_ = -1;
    clear();
    update_lookup_table();
}

void Engine::commit_all() {
    if (!has_pending_input()) return;
    commit_roman();
    get_surrounding_string();
    commit_katakana();
    flush(remove_dash(confirm_candidate()), true);
    update_preedit();
}

bool Engine::has_pending_input() const {
    return !dict_->current().empty() || !roman_text_.empty() || !katakana_text_.empty() ||
           (should_draw_preedit() && has_non_empty_preedit());
}

void Engine::candidate_clicked(size_t index) {
    LOG_DEBUG("do_candidate_clicked(%zu)", index);
    if (dict_->current().empty()) return;
    size_t cursor_pos = lookup_table_.get_cursor_pos();
    size_t page_size = lookup_table_.get_page_size();
    size_t base = cursor_pos - (cursor_pos % page_size);
    cursor_pos = page_size <= index ? index : base + index;
    lookup_table_.set_cursor_pos(cursor_pos);
    dict_->set_current(static_cast<index_t>(cursor_pos));

    ustring current = confirm_candidate();
    commit_string(current);
    if (last(current) == DASH) {
        process_replace(controller_->create_event(Return, 0, 0));
        if (!should_draw_preedit()) flush();
    } else {
        if (commit_roman().empty()) set_completed(current);
        flush();
    }
    if (0 <= cursor_pos_) create_lookup_table();
    update_preedit();
}

bool Engine::cursor_down() {
    selected_ = true;
    if (lookup_table_.cursor_down()) update_candidate();
    return true;
}

bool Engine::cursor_up() {
    selected_ = true;
    if (lookup_table_.cursor_up()) update_candidate();
    return true;
}

bool Engine::page_down() {
    selected_ = true;
    if (lookup_table_.page_down()) update_candidate();
    return true;
}

bool Engine::page_up() {
    selected_ = true;
    if (lookup_table_.page_up()) update_candidate();
    return true;
}

}  // namespace hiragana
