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

#include <cstdint>
#include <string>

#include "Keysyms.h"

namespace hiragana::tsf {

struct KeyEvent {
    keysym::Keysym keyval = keysym::VoidSymbol;
    uint32_t keycode = 0;  // evdev key code
    uint32_t state = 0;    // IBus.ModifierType bits
};

// Translates a WM_KEYDOWN/WM_KEYUP for the engine. xkb is "us" or "jp".
KeyEvent translate_key(WPARAM vk, LPARAM lparam, bool release, const std::string& xkb);

// The keyboard type for layouts/<layout>.<xkb>.json when the setting is "auto".
std::string detect_xkb();

}  // namespace hiragana::tsf
