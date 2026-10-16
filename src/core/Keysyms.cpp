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

#include "Keysyms.h"

#include <cstdio>
#include <utility>

namespace hiragana::keysym {

namespace {

const std::pair<const char*, Keysym> kNames[] = {
    {"space", 0x20}, {"exclam", 0x21}, {"quotedbl", 0x22}, {"numbersign", 0x23}, {"dollar", 0x24},
    {"percent", 0x25}, {"ampersand", 0x26}, {"apostrophe", 0x27}, {"parenleft", 0x28}, {"parenright", 0x29},
    {"asterisk", 0x2a}, {"plus", 0x2b}, {"comma", 0x2c}, {"minus", 0x2d}, {"period", 0x2e}, {"slash", 0x2f},
    {"colon", 0x3a}, {"semicolon", 0x3b}, {"less", 0x3c}, {"equal", 0x3d}, {"greater", 0x3e},
    {"question", 0x3f}, {"at", 0x40}, {"bracketleft", 0x5b}, {"backslash", 0x5c}, {"bracketright", 0x5d},
    {"asciicircum", 0x5e}, {"underscore", 0x5f}, {"grave", 0x60}, {"braceleft", 0x7b}, {"bar", 0x7c},
    {"braceright", 0x7d}, {"asciitilde", 0x7e}, {"yen", 0xa5}, {"periodcentered", 0xb7},
    {"ISO_Level3_Shift", ISO_Level3_Shift}, {"ISO_Left_Tab", ISO_Left_Tab},
    {"BackSpace", BackSpace}, {"Tab", Tab}, {"Return", Return}, {"Escape", Escape}, {"Delete", Delete},
    {"Kanji", Kanji}, {"Muhenkan", Muhenkan}, {"Henkan", Henkan}, {"Henkan_Mode", Henkan},
    {"Hiragana", Hiragana}, {"Katakana", Katakana}, {"Hiragana_Katakana", Hiragana_Katakana},
    {"Zenkaku_Hankaku", Zenkaku_Hankaku}, {"Eisu_toggle", Eisu_toggle}, {"Hangul", Hangul},
    {"Hangul_Hanja", Hangul_Hanja},
    {"Home", Home}, {"Left", Left}, {"Up", Up}, {"Right", Right}, {"Down", Down}, {"Page_Up", Page_Up},
    {"Prior", Page_Up}, {"Page_Down", Page_Down}, {"Next", Page_Down}, {"End", End}, {"Insert", Insert},
    {"KP_Enter", KP_Enter}, {"KP_Page_Up", KP_Page_Up}, {"KP_Page_Down", KP_Page_Down},
    {"Shift_L", Shift_L}, {"Shift_R", Shift_R}, {"Control_L", Control_L}, {"Control_R", Control_R},
    {"Caps_Lock", Caps_Lock}, {"Meta_L", Meta_L}, {"Meta_R", Meta_R}, {"Alt_L", Alt_L}, {"Alt_R", Alt_R},
    {"Super_L", Super_L}, {"Super_R", Super_R},
};

}  // namespace

Keysym from_name(const std::string& n) {
    for (const auto& [key, value] : kNames) {
        if (n == key) return value;
    }
    // Single printable ASCII character such as "a" or "0".
    if (n.size() == 1 && 0x21 <= static_cast<unsigned char>(n[0]) && static_cast<unsigned char>(n[0]) <= 0x7e)
        return static_cast<unsigned char>(n[0]);
    return VoidSymbol;
}

std::string name(Keysym keyval) {
    for (const auto& [key, value] : kNames) {
        if (keyval == value) return key;
    }
    if (0x21 <= keyval && keyval <= 0x7e) return std::string(1, static_cast<char>(keyval));
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%04x", keyval);
    return buf;
}

}  // namespace hiragana::keysym
