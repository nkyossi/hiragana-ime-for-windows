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

// The engine keeps using X11 keysyms and IBus modifier masks so that the
// logic of engine.py and event.py can be ported as is. The TSF layer
// translates Windows virtual keys into these values.

#include <cstdint>
#include <string>

namespace hiragana::keysym {

using Keysym = uint32_t;

constexpr Keysym VoidSymbol = 0xffffff;

constexpr Keysym space = 0x020;
constexpr Keysym exclam = 0x021;
constexpr Keysym backslash = 0x05c;
constexpr Keysym asciitilde = 0x07e;
constexpr Keysym yen = 0x0a5;
constexpr Keysym periodcentered = 0x0b7;

constexpr Keysym ISO_Level3_Shift = 0xfe03;
constexpr Keysym ISO_Left_Tab = 0xfe20;

constexpr Keysym BackSpace = 0xff08;
constexpr Keysym Tab = 0xff09;
constexpr Keysym Return = 0xff0d;
constexpr Keysym Escape = 0xff1b;
constexpr Keysym Delete = 0xffff;

constexpr Keysym Kanji = 0xff21;
constexpr Keysym Muhenkan = 0xff22;
constexpr Keysym Henkan = 0xff23;
constexpr Keysym Hiragana = 0xff25;
constexpr Keysym Katakana = 0xff26;
constexpr Keysym Hiragana_Katakana = 0xff27;
constexpr Keysym Zenkaku_Hankaku = 0xff2a;
constexpr Keysym Eisu_toggle = 0xff30;
constexpr Keysym Hangul = 0xff31;
constexpr Keysym Hangul_Hanja = 0xff34;

constexpr Keysym Home = 0xff50;
constexpr Keysym Left = 0xff51;
constexpr Keysym Up = 0xff52;
constexpr Keysym Right = 0xff53;
constexpr Keysym Down = 0xff54;
constexpr Keysym Page_Up = 0xff55;
constexpr Keysym Page_Down = 0xff56;
constexpr Keysym End = 0xff57;
constexpr Keysym Insert = 0xff63;
constexpr Keysym KP_Enter = 0xff8d;
constexpr Keysym KP_Page_Up = 0xff9a;
constexpr Keysym KP_Page_Down = 0xff9b;

constexpr Keysym Shift_L = 0xffe1;
constexpr Keysym Shift_R = 0xffe2;
constexpr Keysym Control_L = 0xffe3;
constexpr Keysym Control_R = 0xffe4;
constexpr Keysym Caps_Lock = 0xffe5;
constexpr Keysym Meta_L = 0xffe7;
constexpr Keysym Meta_R = 0xffe8;
constexpr Keysym Alt_L = 0xffe9;
constexpr Keysym Alt_R = 0xffea;
constexpr Keysym Super_L = 0xffeb;
constexpr Keysym Super_R = 0xffec;

// IBus.ModifierType
constexpr uint32_t SHIFT_MASK = 1u << 0;
constexpr uint32_t LOCK_MASK = 1u << 1;
constexpr uint32_t CONTROL_MASK = 1u << 2;
constexpr uint32_t MOD1_MASK = 1u << 3;  // Alt
constexpr uint32_t MOD4_MASK = 1u << 6;  // Super
constexpr uint32_t MOD5_MASK = 1u << 7;
constexpr uint32_t RELEASE_MASK = 1u << 30;

// IBus.keyval_from_name(); returns VoidSymbol for an unknown name.
Keysym from_name(const std::string& name);
// IBus.keyval_name() for logging.
std::string name(Keysym keyval);

}  // namespace hiragana::keysym
