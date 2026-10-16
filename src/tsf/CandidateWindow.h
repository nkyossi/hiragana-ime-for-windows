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

#include <functional>
#include <string>
#include <vector>

namespace hiragana::tsf {

// The vertical candidate list (IBus.LookupTable with page size 10).
class CandidateWindow {
public:
    // on_click receives the index within the current page.
    // on_wheel receives +1 to move down and -1 to move up.
    CandidateWindow(std::function<void(size_t)> on_click, std::function<void(int)> on_wheel);
    ~CandidateWindow();
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    void set_candidates(const std::vector<std::wstring>& page, size_t selected, size_t page_index,
                        size_t page_count);
    // Shows the window below (or above) the text rectangle in screen coordinates.
    void show_at(const RECT& text_rect, HWND owner);
    void hide();
    bool visible() const;
    HWND hwnd() const { return hwnd_; }

    // Window classes of a DLL outlive it; call this when the DLL is unloaded.
    static void unregister_class();

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);
    bool create(HWND owner);
    void paint(HDC dc);
    SIZE measure();
    void update_font();
    int row_at(int y) const;

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HFONT font_ = nullptr;
    UINT font_dpi_ = 0;
    std::vector<std::wstring> page_;
    size_t selected_ = 0;
    size_t page_index_ = 0;
    size_t page_count_ = 1;
    int row_height_ = 0;
    int padding_ = 0;
    std::function<void(size_t)> on_click_;
    std::function<void(int)> on_wheel_;
};

}  // namespace hiragana::tsf
