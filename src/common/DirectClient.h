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

// The direct connection between the text service and an editor that wants
// to receive the same requests as an IBus client, e.g. FuriganaPad, which
// adds furigana by watching the reading deleted by the IME and the kanji
// committed after it.
//
// The text service runs in the editor's process. When a key arrives, it sends
// the registered message kGetClientMessage to the focused window:
//
//   wParam: kProtocolVersion
//   lParam: DirectHost* of the text service
//   result: DirectClient* of the window, or 0 if the window does not support it
//
// The pointers are only valid during the call that uses them; the text service
// asks again for every key. Only plain types cross the boundary because the
// DLL and the editor have separate C runtimes.

#include <windows.h>

namespace hiragana::direct {

inline constexpr wchar_t kGetClientMessage[] = L"HiraganaIME.DirectClient";
inline constexpr int kProtocolVersion = 1;

// PreeditSegment::Style of the engine
enum PreeditStyle : int { kInput = 0, kCandidate = 1, kLocked = 2 };

struct PreeditSpan {
    int start;   // UTF-16 offset in the preedit text
    int length;  // UTF-16 units
    int style;   // PreeditStyle
};

class DirectClient;

// Implemented by the text service. client is the caller.
class DirectHost {
public:
    // IBus.Engine.do_reset(), e.g. when the user undoes an edit.
    virtual void __stdcall Reset(DirectClient* client) = 0;
    // Commits the pending conversion, e.g. before the caret moves or the focus leaves.
    virtual void __stdcall CommitAll(DirectClient* client) = 0;

protected:
    ~DirectHost() = default;
};

// Implemented by the editor; the IBus input context signals.
class DirectClient {
public:
    // retrieve-surrounding: copies the text around the cursor (at most
    // capacity - 1 UTF-16 units and a terminating zero) into buffer.
    // Returns the length of the text and sets *cursor to the UTF-16 offset of the cursor.
    virtual int __stdcall GetSurroundingText(wchar_t* buffer, int capacity, int* cursor) = 0;
    // delete-surrounding(offset, n_chars) in characters.
    virtual void __stdcall DeleteSurroundingText(int offset, int count) = 0;
    // commit
    virtual void __stdcall CommitText(const wchar_t* text, int length) = 0;
    // preedit-changed; an empty text hides the preedit. cursor is a UTF-16 offset.
    virtual void __stdcall UpdatePreedit(const wchar_t* text, int length, const PreeditSpan* spans, int span_count,
                                         int cursor) = 0;
    // The caret in screen coordinates for the candidate window.
    virtual BOOL __stdcall GetCaretRect(RECT* rect) = 0;
    // The host to call back; nullptr when the text service goes away.
    virtual void __stdcall SetHost(DirectHost* host) = 0;

protected:
    ~DirectClient() = default;
};

// Asks hwnd for its DirectClient. Returns nullptr if it has none.
inline DirectClient* query_client(HWND hwnd, DirectHost* host) {
    static const UINT message = RegisterWindowMessageW(kGetClientMessage);
    if (!hwnd || !message) return nullptr;
    DWORD pid = 0;
    if (GetWindowThreadProcessId(hwnd, &pid) != GetCurrentThreadId() || pid != GetCurrentProcessId())
        return nullptr;
    return reinterpret_cast<DirectClient*>(
        SendMessageW(hwnd, message, kProtocolVersion, reinterpret_cast<LPARAM>(host)));
}

}  // namespace hiragana::direct
