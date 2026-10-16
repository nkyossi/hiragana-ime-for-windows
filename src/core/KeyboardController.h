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

// Port of engine/event.py and the layout loading part of engine.py.

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Json.h"
#include "Keysyms.h"
#include "Text.h"

namespace hiragana {

// Input modes: 'A', 'あ', 'ア', 'Ａ', 'ｱ'
using Mode = char32_t;
inline constexpr Mode MODE_ALPHANUMERIC = U'A';
inline constexpr Mode MODE_HIRAGANA = U'あ';
inline constexpr Mode MODE_KATAKANA = U'ア';
inline constexpr Mode MODE_WIDE_ALPHANUMERIC = U'Ａ';
inline constexpr Mode MODE_HALFWIDTH_KATAKANA = U'ｱ';
bool is_valid_mode(Mode mode);

struct Layout {
    json::Value data;  // the merged layout file
    std::unordered_map<ustring, ustring> roomazi;
    std::vector<std::array<ustring, 4>> key;    // indexed by the evdev key code
    std::vector<std::array<ustring, 4>> altgr;  // indexed by the evdev key code
    bool has_key = false;
    bool has_altgr = false;
    bool has_roomazi = false;
    bool is_kana = false;
    ustring tiny;  // the 'Tiny' key, e.g. "l"
};

// Loads <dir>/layouts/<layout>.<xkb>.json, falling back to roomazi.<xkb>.json,
// and merges <altgr>.<xkb>.json into it. The directories are searched in order.
Layout load_layout(const std::vector<std::filesystem::path>& data_dirs, const std::string& layout,
                   const std::string& altgr, const std::string& xkb);

class KeyboardController;

class Event {
public:
    Event(const KeyboardController* controller, keysym::Keysym keyval, uint32_t keycode, uint32_t state,
          uint32_t modifiers)
        : controller_(controller), keyval(keyval), keycode(keycode), state(state), modifiers(modifiers) {}

    bool has_altgr() const;
    bool is_release() const;
    bool is_press() const { return !is_release(); }
    bool is_key(keysym::Keysym k) const;
    bool is_space() const;
    bool is_backspace() const;
    bool is_ascii() const;
    bool is_modifier() const;
    bool is_prefix() const;
    bool is_prefixed() const;
    bool is_control() const;
    bool is_shift() const;
    bool is_katakana() const;
    bool is_henkan() const;
    bool is_muhenkan() const;
    bool is_dual_role() const;
    ustring get_string() const;

private:
    const KeyboardController* controller_;

public:
    keysym::Keysym keyval;
    uint32_t keycode;  // evdev key code (same as the PC/AT set 1 scan code for the main keys)
    uint32_t state;    // IBus.ModifierType bits
    uint32_t modifiers;
};

// The engine side of KeyboardController::process_key_event().
class KeyHandler {
public:
    virtual ~KeyHandler() = default;
    virtual Mode get_mode() const = 0;
    virtual void set_mode(Mode mode, bool override) = 0;
    virtual bool is_lookup_table_visible() const = 0;
    virtual bool process_key_event(const Event& e) = 0;
};

class KeyboardController {
public:
    explicit KeyboardController(Layout layout);

    void reset() { modifiers_ = 0; }
    bool process_key_event(KeyHandler& engine, keysym::Keysym keyval, uint32_t keycode, uint32_t state);
    bool is_onoff_by_caps() const { return on_off_by_caps_; }
    Event create_event(keysym::Keysym keyval, uint32_t keycode, uint32_t state) const;
    ustring get_string(const Event& e) const;
    ustring kana(const Event& e) const;
    ustring transliterate_back(const ustring& roman) const;
    const Layout& layout() const { return layout_; }

    // True if a key release of keyval needs to reach process_key_event() (dual-role keys).
    bool tracks_release(keysym::Keysym keyval) const;
    // Updates the modifier state for a key event that the engine does not need
    // to see. It has the same effect on the state as process_key_event() for a
    // non-modifier key pressed in the alphanumeric mode, or released.
    void track_trivial_key(keysym::Keysym keyval, uint32_t state);
    uint32_t modifiers() const { return modifiers_; }

private:
    friend class Event;

    Layout layout_;
    uint32_t modifiers_ = 0;

    bool on_off_by_caps_ = true;
    bool sands_ = false;
    keysym::Keysym henkan_ = keysym::VoidSymbol;
    keysym::Keysym muhenkan_ = keysym::VoidSymbol;
    keysym::Keysym eisuu_ = keysym::VoidSymbol;
    keysym::Keysym kana_ = keysym::VoidSymbol;
    keysym::Keysym space_ = keysym::VoidSymbol;
    bool prefix_ = false;
    bool has_yen_ = false;
    uint32_t dual_bits_ = 0;
    bool capture_alt_r_ = false;
};

}  // namespace hiragana
