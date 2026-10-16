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

#include "KeyboardController.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Log.h"

namespace hiragana {

namespace fs = std::filesystem;
using namespace keysym;

namespace {

const Keysym MODIFIERS[] = {Shift_L, Shift_R, Control_L, Control_R, Alt_L, Alt_R};

constexpr uint32_t SHIFT_L_BIT = 0x01;
constexpr uint32_t SHIFT_R_BIT = 0x02;
constexpr uint32_t CONTROL_L_BIT = 0x04;
constexpr uint32_t CONTROL_R_BIT = 0x08;
constexpr uint32_t ALT_L_BIT = 0x10;
constexpr uint32_t ALT_R_BIT = 0x20;
constexpr uint32_t SPACE_BIT = 0x40;
constexpr uint32_t PREFIX_BIT = 0x80;
constexpr uint32_t SHIFT_BITS = SHIFT_L_BIT | SHIFT_R_BIT;
constexpr uint32_t CONTROL_BITS = CONTROL_L_BIT | CONTROL_R_BIT;
constexpr uint32_t MODIFIER_BITS = SHIFT_BITS | CONTROL_L_BIT | CONTROL_R_BIT | ALT_L_BIT | ALT_R_BIT | SPACE_BIT;

constexpr uint32_t DUAL_SHIFT_L_BIT = SHIFT_L_BIT << 8;
constexpr uint32_t DUAL_SHIFT_R_BIT = SHIFT_R_BIT << 8;
constexpr uint32_t DUAL_CONTROL_R_BIT = CONTROL_R_BIT << 8;
constexpr uint32_t DUAL_ALT_R_BIT = ALT_R_BIT << 8;
constexpr uint32_t DUAL_SPACE_BIT = SPACE_BIT << 8;
constexpr uint32_t DUAL_BITS = DUAL_SPACE_BIT | DUAL_SHIFT_L_BIT | DUAL_SHIFT_R_BIT | DUAL_CONTROL_R_BIT | DUAL_ALT_R_BIT;

constexpr uint32_t NOT_DUAL_SHIFT_L_BIT = SHIFT_L_BIT << 16;
constexpr uint32_t NOT_DUAL_SHIFT_R_BIT = SHIFT_R_BIT << 16;
constexpr uint32_t NOT_DUAL_CONTROL_R_BIT = CONTROL_R_BIT << 16;
constexpr uint32_t NOT_DUAL_ALT_R_BIT = ALT_R_BIT << 16;
constexpr uint32_t NOT_DUAL_SPACE_BIT = SPACE_BIT << 16;

bool is_modifier_key(Keysym k) {
    return std::find(std::begin(MODIFIERS), std::end(MODIFIERS), k) != std::end(MODIFIERS);
}

json::Value load_json(const fs::path& path) {
    LOG_DEBUG("_load_json(\"%s\")", path.u8string().c_str());
    std::ifstream f(path, std::ios::binary);
    if (!f) return json::Value();
    std::ostringstream ss;
    ss << f.rdbuf();
    try {
        return json::parse(ss.str());
    } catch (const std::exception& e) {
        LOG_ERROR("could not load \"%s\": %s", path.u8string().c_str(), e.what());
        return json::Value();
    }
}

fs::path find_in_data_dirs(const std::vector<fs::path>& dirs, const std::string& file) {
    for (const auto& dir : dirs) {
        fs::path path = dir / "layouts" / fs::u8path(file);
        std::error_code ec;
        if (fs::exists(path, ec)) return path;
    }
    return fs::path();
}

std::vector<std::array<ustring, 4>> load_key_table(const json::Value& v) {
    std::vector<std::array<ustring, 4>> table;
    for (const auto& row : v.as_array()) {
        std::array<ustring, 4> entry;
        const auto& cols = row.as_array();
        for (size_t i = 0; i < 4 && i < cols.size(); ++i) entry[i] = from_utf8(cols[i].as_string());
        table.push_back(entry);
    }
    return table;
}

}  // namespace

bool is_valid_mode(Mode mode) {
    return mode == MODE_ALPHANUMERIC || mode == MODE_HIRAGANA || mode == MODE_KATAKANA ||
           mode == MODE_WIDE_ALPHANUMERIC || mode == MODE_HALFWIDTH_KATAKANA;
}

Layout load_layout(const std::vector<fs::path>& data_dirs, const std::string& layout_name,
                   const std::string& altgr_name, const std::string& xkb_in) {
    std::string xkb = (xkb_in == "us" || xkb_in == "jp" || xkb_in == "us+dvorak") ? xkb_in : "us";
    Layout layout;
    fs::path path = find_in_data_dirs(data_dirs, layout_name + "." + xkb + ".json");
    LOG_INFO("keyboard layout: \"%s\"", path.u8string().c_str());
    json::Value data = path.empty() ? json::Value() : load_json(path);
    if (!data.is_object()) {
        std::string fallback_xkb = (xkb == "jp") ? "jp" : "us";
        data = load_json(find_in_data_dirs(data_dirs, "roomazi." + fallback_xkb + ".json"));
    }
    fs::path altgr_path = find_in_data_dirs(data_dirs, altgr_name + "." + xkb + ".json");
    json::Value altgr = altgr_path.empty() ? json::Value() : load_json(altgr_path);
    if (altgr.is_object()) {
        for (const auto& [k, v] : altgr.as_object()) data.set(k, v);
    }
    if (!data.is_object()) data = json::Value(json::Value::Object{});

    layout.data = data;
    layout.is_kana = data["Type"].as_string() == "Kana";
    layout.has_roomazi = data.contains("Roomazi");
    for (const auto& [k, v] : data["Roomazi"].as_object()) layout.roomazi[from_utf8(k)] = from_utf8(v.as_string());
    layout.has_key = data.contains("Key");
    layout.key = load_key_table(data["Key"]);
    layout.has_altgr = data.contains("AltGr");
    layout.altgr = load_key_table(data["AltGr"]);
    layout.tiny = from_utf8(data["Tiny"].as_string());
    return layout;
}

//
// Event
//
bool Event::has_altgr() const { return modifiers & ALT_R_BIT; }

bool Event::is_release() const { return state & RELEASE_MASK; }

bool Event::is_key(Keysym k) const {
    if (k == VoidSymbol) return false;
    if (!is_modifier() && k == keyval) return true;
    if (k == Shift_L && (modifiers & DUAL_SHIFT_L_BIT)) return true;
    if (k == Shift_R && (modifiers & DUAL_SHIFT_R_BIT)) return true;
    if (k == Control_R && (modifiers & DUAL_CONTROL_R_BIT)) return true;
    if (k == Alt_R && (modifiers & DUAL_ALT_R_BIT)) return true;
    return false;
}

bool Event::is_space() const {
    if (is_key(controller_->space_)) return true;
    if (keyval == space) return !(controller_->prefix_ && is_release());
    return false;
}

bool Event::is_backspace() const { return keyval == BackSpace; }

bool Event::is_ascii() const {
    // IBus.yen is treated as '¥' for Japanese 109 keyboard.
    return (exclam <= keyval && keyval <= asciitilde) || keyval == yen || keyval == periodcentered || is_space();
}

bool Event::is_modifier() const { return is_modifier_key(keyval); }

bool Event::is_prefix() const {
    return controller_->prefix_ && keyval == space && (modifiers & PREFIX_BIT) && is_release();
}

bool Event::is_prefixed() const { return controller_->prefix_ && (modifiers & PREFIX_BIT); }

bool Event::is_control() const {
    uint32_t mask = CONTROL_BITS;
    if (keyval == Control_R && (modifiers & DUAL_CONTROL_R_BIT)) mask &= ~CONTROL_R_BIT;
    return modifiers & mask;
}

bool Event::is_shift() const {
    uint32_t mask = SHIFT_BITS;
    if (controller_->sands_ && (modifiers & SPACE_BIT)) return true;
    if (controller_->prefix_ && (modifiers & (SPACE_BIT | PREFIX_BIT))) return true;
    if (keyval == Shift_R && (modifiers & DUAL_SHIFT_R_BIT)) mask &= ~SHIFT_R_BIT;
    if (keyval == Shift_L && (modifiers & DUAL_SHIFT_L_BIT)) mask &= ~SHIFT_L_BIT;
    return modifiers & mask;
}

bool Event::is_katakana() const { return is_key(controller_->kana_) || is_key(Hiragana_Katakana); }

bool Event::is_henkan() const {
    if (is_key(controller_->henkan_) || is_key(Henkan) || is_key(Hangul)) return !(is_control() || is_shift());
    return false;
}

bool Event::is_muhenkan() const {
    if (is_key(controller_->muhenkan_)) return true;
    if (is_key(controller_->henkan_) || is_key(Henkan) || is_key(Hangul)) return is_shift() && !is_control();
    return false;
}

bool Event::is_dual_role() const { return modifiers & DUAL_BITS; }

ustring Event::get_string() const { return controller_->get_string(*this); }

//
// KeyboardController
//
KeyboardController::KeyboardController(Layout layout) : layout_(std::move(layout)) {
    const json::Value& d = layout_.data;
    if (d["Keyboard"].as_string() == "109") {
        kana_ = Hiragana_Katakana;
        eisuu_ = Eisu_toggle;
    }
    on_off_by_caps_ = d["OnOffByCaps"].as_bool(on_off_by_caps_);
    has_yen_ = d["HasYen"].as_bool(has_yen_);

    sands_ = d["SandS"].as_bool(false);
    if (sands_) {
        dual_bits_ |= DUAL_SPACE_BIT;
    } else {
        prefix_ = d["Prefix"].as_bool(false);
        if (prefix_) dual_bits_ |= DUAL_SPACE_BIT;
    }

    if (d.contains("Space")) space_ = from_name(d["Space"].as_string());
    if (d.contains("Henkan")) henkan_ = from_name(d["Henkan"].as_string());
    if (d.contains("Muhenkan")) muhenkan_ = from_name(d["Muhenkan"].as_string());
    if (d.contains("Katakana")) kana_ = from_name(d["Katakana"].as_string());

    // Check dual role modifiers
    for (Keysym k : {henkan_, muhenkan_, kana_, space_}) {
        auto it = std::find(std::begin(MODIFIERS), std::end(MODIFIERS), k);
        if (it != std::end(MODIFIERS)) dual_bits_ |= DUAL_SHIFT_L_BIT << (it - std::begin(MODIFIERS));
        if (k == Alt_R) capture_alt_r_ = true;
    }
}

bool KeyboardController::tracks_release(Keysym keyval) const {
    return is_modifier_key(keyval) || keyval == space || keyval == Caps_Lock;
}

void KeyboardController::track_trivial_key(Keysym keyval, uint32_t state) {
    modifiers_ &= ~DUAL_BITS;
    if (state & RELEASE_MASK) {
        // A normal key release clears the prefix lock; see process_key_event().
        if (!(modifiers_ & dual_bits_)) modifiers_ &= ~PREFIX_BIT;
        return;
    }
    if (state & SHIFT_MASK) {
        modifiers_ |= SHIFT_L_BIT;
    } else if (modifiers_ & SHIFT_L_BIT) {
        modifiers_ &= ~SHIFT_L_BIT;
    }
    if ((modifiers_ & SPACE_BIT) && keyval != space) modifiers_ |= NOT_DUAL_SPACE_BIT;
    if ((modifiers_ & SHIFT_L_BIT) && keyval != Shift_L) modifiers_ |= NOT_DUAL_SHIFT_L_BIT;
    if ((modifiers_ & SHIFT_R_BIT) && keyval != Shift_R) modifiers_ |= NOT_DUAL_SHIFT_R_BIT;
    if ((modifiers_ & CONTROL_R_BIT) && keyval != Control_R) modifiers_ |= NOT_DUAL_CONTROL_R_BIT;
    if ((modifiers_ & ALT_R_BIT) && keyval != Alt_R) modifiers_ |= NOT_DUAL_ALT_R_BIT;
}

bool KeyboardController::process_key_event(KeyHandler& engine, Keysym keyval, uint32_t keycode, uint32_t state) {
    LOG_DEBUG("process_key_event: %#07x", modifiers_);

    // Ignore XFree86 anomaly
    if (keyval == ISO_Left_Tab) {
        keyval = Tab;
    } else if (keyval == Meta_L) {
        keyval = Alt_L;
    } else if (keyval == Meta_R || keyval == ISO_Level3_Shift) {
        keyval = Alt_R;
    }

    modifiers_ &= ~DUAL_BITS;
    bool is_press = !(state & RELEASE_MASK);
    if (is_press) {
        // Test state first to support On-Screen Keyboard since it may
        // not generate key events for shift keys.
        if (state & SHIFT_MASK) {
            modifiers_ |= SHIFT_L_BIT;
        } else if (modifiers_ & SHIFT_L_BIT) {
            modifiers_ &= ~SHIFT_L_BIT;
        }

        if (keyval == space) {
            if (modifiers_ & (MODIFIER_BITS & ~SPACE_BIT))
                modifiers_ |= NOT_DUAL_SPACE_BIT;
            else
                modifiers_ &= ~NOT_DUAL_SPACE_BIT;
            modifiers_ |= SPACE_BIT;
        } else if (keyval == Shift_L) {
            if (modifiers_ & MODIFIER_BITS)
                modifiers_ |= NOT_DUAL_SHIFT_L_BIT;
            else
                modifiers_ &= ~NOT_DUAL_SHIFT_L_BIT;
            modifiers_ |= SHIFT_L_BIT;
        } else if (keyval == Shift_R) {
            if (modifiers_ & MODIFIER_BITS)
                modifiers_ |= NOT_DUAL_SHIFT_R_BIT;
            else
                modifiers_ &= ~NOT_DUAL_SHIFT_R_BIT;
            modifiers_ |= SHIFT_R_BIT;
        } else if (keyval == Control_L) {
            modifiers_ |= CONTROL_L_BIT;
        } else if (keyval == Control_R) {
            if (modifiers_ & MODIFIER_BITS)
                modifiers_ |= NOT_DUAL_CONTROL_R_BIT;
            else
                modifiers_ &= ~NOT_DUAL_CONTROL_R_BIT;
            modifiers_ |= CONTROL_R_BIT;
        } else if (keyval == Alt_R) {
            if (modifiers_ & MODIFIER_BITS)
                modifiers_ |= NOT_DUAL_ALT_R_BIT;
            else
                modifiers_ &= ~NOT_DUAL_ALT_R_BIT;
            modifiers_ |= ALT_R_BIT;
        }

        if ((modifiers_ & SPACE_BIT) && keyval != space) modifiers_ |= NOT_DUAL_SPACE_BIT;
        if ((modifiers_ & SHIFT_L_BIT) && keyval != Shift_L) modifiers_ |= NOT_DUAL_SHIFT_L_BIT;
        if ((modifiers_ & SHIFT_R_BIT) && keyval != Shift_R) modifiers_ |= NOT_DUAL_SHIFT_R_BIT;
        if ((modifiers_ & CONTROL_R_BIT) && keyval != Control_R) modifiers_ |= NOT_DUAL_CONTROL_R_BIT;
        if ((modifiers_ & ALT_R_BIT) && keyval != Alt_R) modifiers_ |= NOT_DUAL_ALT_R_BIT;

        if (on_off_by_caps_ && keyval == Caps_Lock) {
            // The text service sets the mode from the Caps Lock state after
            // Windows has toggled it; see Engine::set_mode_by_caps_lock().
            return false;
        }

        if (keyval == Muhenkan || keyval == Hangul_Hanja) {
            // [無変換], [A]
            Mode mode;
            if (modifiers_ & SHIFT_BITS) {
                mode = MODE_WIDE_ALPHANUMERIC;
            } else if (modifiers_ & (CONTROL_L_BIT | CONTROL_R_BIT)) {
                Mode m = engine.get_mode();
                mode = m == MODE_KATAKANA            ? MODE_HALFWIDTH_KATAKANA
                       : m == MODE_HALFWIDTH_KATAKANA ? MODE_HIRAGANA
                       : m == MODE_HIRAGANA           ? MODE_KATAKANA
                                                      : MODE_HIRAGANA;
            } else {
                mode = MODE_ALPHANUMERIC;
            }
            // Note EngineHiragana.set_mode() returns None, so the key is
            // processed further as in event.py.
            engine.set_mode(mode, true);
        } else if (keyval == Henkan || keyval == Hangul || keyval == Hiragana_Katakana) {
            // [変換], [あ], [ひらがな]
            if (!engine.is_lookup_table_visible()) {
                Mode mode = MODE_HIRAGANA;
                if (keyval == Hiragana_Katakana && (modifiers_ & SHIFT_BITS)) mode = MODE_KATAKANA;
                engine.set_mode(mode, true);
            }
        } else if (keyval == eisuu_ || keyval == Zenkaku_Hankaku) {
            // [英数], [全角/半角]
            Mode mode = engine.get_mode() != MODE_ALPHANUMERIC ? MODE_ALPHANUMERIC : MODE_HIRAGANA;
            engine.set_mode(mode, true);
            return true;
        }

    } else {
        if (keyval == space) {
            if (!(modifiers_ & NOT_DUAL_SPACE_BIT)) modifiers_ |= DUAL_SPACE_BIT;
            modifiers_ &= ~SPACE_BIT;
        } else if (keyval == Shift_L) {
            if (!(modifiers_ & NOT_DUAL_SHIFT_L_BIT)) modifiers_ |= DUAL_SHIFT_L_BIT;
            modifiers_ &= ~SHIFT_L_BIT;
        } else if (keyval == Shift_R) {
            if (!(modifiers_ & NOT_DUAL_SHIFT_R_BIT)) modifiers_ |= DUAL_SHIFT_R_BIT;
            modifiers_ &= ~SHIFT_R_BIT;
        } else if (keyval == Control_L) {
            modifiers_ &= ~CONTROL_L_BIT;
        } else if (keyval == Control_R) {
            if (!(modifiers_ & NOT_DUAL_CONTROL_R_BIT)) modifiers_ |= DUAL_CONTROL_R_BIT;
            modifiers_ &= ~CONTROL_R_BIT;
        } else if (keyval == Alt_R) {
            if (!(modifiers_ & NOT_DUAL_ALT_R_BIT)) modifiers_ |= DUAL_ALT_R_BIT;
            modifiers_ &= ~ALT_R_BIT;
        }
    }

    Mode mode = engine.get_mode();
    if (mode == MODE_HIRAGANA || mode == MODE_KATAKANA || mode == MODE_HALFWIDTH_KATAKANA) {
        if (keyval == space) {
            if (sands_ && is_press) {
                return true;
            } else if (prefix_) {
                if (is_press) {
                    if (!(modifiers_ & (NOT_DUAL_SPACE_BIT | PREFIX_BIT))) return true;
                } else {
                    if (modifiers_ & DUAL_SPACE_BIT)
                        modifiers_ ^= PREFIX_BIT;
                    else
                        return true;
                }
            }
        }
    }

    // Ignore normal key release events
    if (!is_press && !(modifiers_ & dual_bits_)) {
        modifiers_ &= ~PREFIX_BIT;
        return false;
    }

    // Treat Yen key separately for Japanese 109 keyboard.
    if (keyval == backslash && keycode == 0x7c) keyval = yen;

    // Call back the engine with an event
    Event event = create_event(keyval, keycode, state);
    bool processed = engine.process_key_event(event);

    if (keyval == Alt_R && capture_alt_r_) return true;
    if (event.is_dual_role()) {
        // Modifiers have to be further processed.
        return false;
    }
    return processed;
}

Event KeyboardController::create_event(Keysym keyval, uint32_t keycode, uint32_t state) const {
    return Event(this, keyval, keycode, state, modifiers_);
}

ustring KeyboardController::get_string(const Event& e) const {
    ustring c;
    if (e.has_altgr() && layout_.has_altgr) {
        if (e.keycode < layout_.altgr.size()) {
            const auto& a = layout_.altgr[e.keycode];
            c = e.is_shift() ? a[3] : a[2];
        }
    } else if (e.is_ascii()) {
        Keysym keyval = e.is_space() ? space : e.keyval;
        if (keyval == yen) {
            c = U"¥";
        } else if (keyval == asciitilde && e.keycode == 0x0b) {
            c = U"_";
        } else {
            c = ustring(1, static_cast<char32_t>(keyval));
        }
    }
    return c;
}

ustring KeyboardController::kana(const Event& e) const {
    ustring c;
    if (layout_.has_key && e.keycode < layout_.key.size()) {
        const auto& a = layout_.key[e.keycode];
        c = e.is_shift() ? a[3] : a[2];
    }
    return c;
}

ustring KeyboardController::transliterate_back(const ustring& roman) const {
    auto it = layout_.roomazi.find(roman);
    return it == layout_.roomazi.end() ? ustring() : it->second;
}

}  // namespace hiragana
