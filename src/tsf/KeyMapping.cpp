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

#include "KeyMapping.h"

namespace hiragana::tsf {

using namespace keysym;

namespace {

// Virtual keys of Japanese keyboards that winuser.h names only for some SDKs.
constexpr UINT VK_DBE_ALPHANUMERIC_ = 0xF0;
constexpr UINT VK_DBE_KATAKANA_ = 0xF1;
constexpr UINT VK_DBE_HIRAGANA_ = 0xF2;
constexpr UINT VK_DBE_SBCSCHAR_ = 0xF3;
constexpr UINT VK_DBE_DBCSCHAR_ = 0xF4;

// The PC/AT set 1 scan code -> the Linux evdev key code. The layout files are
// indexed by the evdev code, which equals the scan code for the main keys.
uint32_t evdev_from_scancode(UINT scancode, bool extended) {
    if (extended) {
        switch (scancode) {
            case 0x1C: return 96;   // KP Enter
            case 0x1D: return 97;   // Right Ctrl
            case 0x35: return 98;   // KP /
            case 0x38: return 100;  // Right Alt
            case 0x47: return 102;  // Home
            case 0x48: return 103;  // Up
            case 0x49: return 104;  // Page Up
            case 0x4B: return 105;  // Left
            case 0x4D: return 106;  // Right
            case 0x4F: return 107;  // End
            case 0x50: return 108;  // Down
            case 0x51: return 109;  // Page Down
            case 0x52: return 110;  // Insert
            case 0x53: return 111;  // Delete
            case 0x5B: return 125;  // Left Windows
            case 0x5C: return 126;  // Right Windows
            case 0x5D: return 127;  // Menu
            default: return 0;
        }
    }
    switch (scancode) {
        case 0x70: return 93;   // カタカナ/ひらがな
        case 0x73: return 89;   // ろ (\_)
        case 0x79: return 92;   // 変換
        case 0x7B: return 94;   // 無変換
        case 0x7D: return 124;  // ￥
        default: return scancode <= 88 ? scancode : 0;
    }
}

Keysym from_char(wchar_t c) {
    if (c < 0x20) return VoidSymbol;
    if (c < 0x100) return c;               // ASCII and Latin-1 match the keysyms
    return 0x01000000u | static_cast<Keysym>(c);  // X11 Unicode keysym
}

Keysym keysym_of(UINT vk, UINT scancode, bool extended, bool shift, bool caps_lock) {
    switch (vk) {
        case VK_BACK: return BackSpace;
        case VK_TAB: return Tab;
        case VK_RETURN: return Return;
        case VK_ESCAPE: return Escape;
        case VK_DELETE: return Delete;
        case VK_SPACE: return space;
        case VK_PRIOR: return Page_Up;
        case VK_NEXT: return Page_Down;
        case VK_END: return End;
        case VK_HOME: return Home;
        case VK_LEFT: return Left;
        case VK_UP: return Up;
        case VK_RIGHT: return Right;
        case VK_DOWN: return Down;
        case VK_INSERT: return Insert;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return (vk == VK_RSHIFT || scancode == 0x36) ? Shift_R : Shift_L;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return (vk == VK_RCONTROL || extended) ? Control_R : Control_L;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return (vk == VK_RMENU || extended) ? Alt_R : Alt_L;
        case VK_CAPITAL: return Caps_Lock;
        case VK_LWIN: return Super_L;
        case VK_RWIN: return Super_R;
        case VK_CONVERT: return Henkan;
        case VK_NONCONVERT: return Muhenkan;
        case VK_KANA:
        case VK_DBE_HIRAGANA_:
        case VK_DBE_KATAKANA_:
            return Hiragana_Katakana;
        case VK_DBE_ALPHANUMERIC_: return Eisu_toggle;
        case VK_KANJI:
        case VK_DBE_SBCSCHAR_:
        case VK_DBE_DBCSCHAR_:
            return Zenkaku_Hankaku;
        default:
            break;
    }

    // Printable keys: the character of the active keyboard layout with Shift
    // (and Caps Lock) applied, ignoring Ctrl and Alt.
    BYTE keys[256] = {};
    if (shift) keys[VK_SHIFT] = 0x80;
    if (caps_lock) keys[VK_CAPITAL] = 0x01;
    wchar_t buffer[8] = {};
    // Flag 4: do not change the keyboard state (Windows 10 1607 or later).
    int n = ToUnicodeEx(vk, scancode, keys, buffer, static_cast<int>(std::size(buffer)), 4, GetKeyboardLayout(0));
    if (n == 1) return from_char(buffer[0]);
    // Other keys (F1, ...) get a keysym that is neither a character nor one the engine knows.
    return 0x10000000u | vk;
}

}  // namespace

KeyEvent translate_key(WPARAM wparam, LPARAM lparam, bool release, const std::string& xkb) {
    KeyEvent e;
    UINT vk = static_cast<UINT>(wparam);
    UINT scancode = (lparam >> 16) & 0xFF;
    bool extended = (lparam >> 24) & 1;
    if (scancode == 0) scancode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    e.keycode = evdev_from_scancode(scancode, extended);

    bool left_shift = GetKeyState(VK_LSHIFT) < 0;
    bool right_shift = GetKeyState(VK_RSHIFT) < 0;
    bool left_control = GetKeyState(VK_LCONTROL) < 0;
    bool right_control = GetKeyState(VK_RCONTROL) < 0;
    bool left_alt = GetKeyState(VK_LMENU) < 0;
    bool right_alt = GetKeyState(VK_RMENU) < 0;
    bool caps_lock = (GetKeyState(VK_CAPITAL) & 1) != 0;

    e.keyval = keysym_of(vk, scancode, extended, left_shift || right_shift, caps_lock);

    // On the Japanese keyboard, Shift+0 has no character on Windows while it
    // is asciitilde with XKB. engine.py turns it into '_', and the kana
    // layouts use it for 'を'.
    if (xkb == "jp" && e.keycode == 11 && (left_shift || right_shift)) e.keyval = asciitilde;

    // Like X11, report the modifier state before the event: a modifier key's
    // own bit is clear when it is pressed and set when it is released.
    // event.py depends on this to tell a tap of right Shift from Shift+key.
    switch (e.keyval) {
        case Shift_L: left_shift = release; break;
        case Shift_R: right_shift = release; break;
        case Control_L: left_control = release; break;
        case Control_R: right_control = release; break;
        case Alt_L: left_alt = release; break;
        case Alt_R: right_alt = release; break;
        default: break;
    }
    // Some layouts report AltGr as Left Ctrl + Right Alt.
    if (right_alt && left_control && !right_control) left_control = false;

    if (left_shift || right_shift) e.state |= SHIFT_MASK;
    if (left_control || right_control) e.state |= CONTROL_MASK;
    // Right Alt works as AltGr (ISO_Level3_Shift) as on Linux, so only Left Alt is Mod1.
    if (left_alt) e.state |= MOD1_MASK;
    if (right_alt) e.state |= MOD5_MASK;
    if (caps_lock) e.state |= LOCK_MASK;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) e.state |= MOD4_MASK;
    if (release) e.state |= RELEASE_MASK;
    return e;
}

std::string detect_xkb() {
    // The keyboard driver of the Japanese layout: kbd106.dll or kbd101.dll.
    wchar_t driver[64] = {};
    DWORD size = sizeof driver;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\i8042prt\\Parameters",
                     L"LayerDriver JPN", RRF_RT_REG_SZ, nullptr, driver, &size) == ERROR_SUCCESS) {
        std::wstring d(driver);
        for (auto& c : d) c = static_cast<wchar_t>(towlower(c));
        if (d.find(L"kbd106") != std::wstring::npos) return "jp";
        if (d.find(L"kbd101") != std::wstring::npos) return "us";
    }
    return GetKeyboardType(0) == 7 ? "jp" : "us";
}

}  // namespace hiragana::tsf
