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

#include "CandidateWindow.h"

#include <windowsx.h>

#include <algorithm>

#include "Globals.h"

namespace hiragana::tsf {

namespace {

const wchar_t kClassName[] = L"HiraganaIMECandidateWindow";

UINT dpi_for(HWND hwnd, const POINT& pt) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    UINT dpi = 0;
    if (get_dpi && hwnd) dpi = get_dpi(hwnd);
    if (dpi == 0) {
        HDC dc = GetDC(nullptr);
        dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
        ReleaseDC(nullptr, dc);
    }
    (void)pt;
    return dpi ? dpi : 96;
}

}  // namespace

CandidateWindow::CandidateWindow(std::function<void(size_t)> on_click, std::function<void(int)> on_wheel)
    : on_click_(std::move(on_click)), on_wheel_(std::move(on_wheel)) {}

CandidateWindow::~CandidateWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    if (font_) DeleteObject(font_);
}

void CandidateWindow::unregister_class() { UnregisterClassW(kClassName, g_module); }

bool CandidateWindow::create(HWND owner) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc = {sizeof wc};
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW | CS_IME;
        wc.lpfnWndProc = window_proc;
        wc.hInstance = g_module;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        atom = RegisterClassExW(&wc);
        if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        atom = 1;
    }
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP | WS_BORDER,
                            0, 0, 1, 1, owner, nullptr, g_module, this);
    owner_ = owner;
    return hwnd_ != nullptr;
}

void CandidateWindow::set_candidates(const std::vector<std::wstring>& page, size_t selected, size_t page_index,
                                     size_t page_count) {
    page_ = page;
    selected_ = selected;
    page_index_ = page_index;
    page_count_ = page_count ? page_count : 1;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::update_font() {
    UINT dpi = dpi_for(hwnd_, POINT{});
    if (font_ && dpi == font_dpi_) return;
    if (font_) DeleteObject(font_);
    font_dpi_ = dpi;
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(11, static_cast<int>(dpi), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Yu Gothic UI");
    font_ = CreateFontIndirectW(&lf);
    padding_ = MulDiv(4, static_cast<int>(dpi), 96);
}

SIZE CandidateWindow::measure() {
    update_font();
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, font_);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    row_height_ = tm.tmHeight + padding_;
    int width = MulDiv(80, static_cast<int>(font_dpi_), 96);
    for (const auto& s : page_) {
        SIZE size;
        GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &size);
        width = (std::max)(width, static_cast<int>(size.cx));
    }
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    int rows = static_cast<int>(page_.size()) + (page_count_ > 1 ? 1 : 0);
    return SIZE{width + padding_ * 4, rows * row_height_ + padding_ * 2};
}

void CandidateWindow::show_at(const RECT& text_rect, HWND owner) {
    if (!hwnd_ || owner != owner_) {
        if (hwnd_) {
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        if (!create(owner)) return;
    }
    SIZE size = measure();
    POINT pt = {text_rect.left, text_rect.bottom};
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(monitor, &mi);
    const RECT& work = mi.rcWork;
    int x = text_rect.left;
    int y = text_rect.bottom + 2;
    if (y + size.cy > work.bottom) y = text_rect.top - size.cy - 2;
    if (x + size.cx > work.right) x = work.right - size.cx;
    x = (std::max)(x, static_cast<int>(work.left));
    y = (std::max)(y, static_cast<int>(work.top));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool CandidateWindow::visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

int CandidateWindow::row_at(int y) const {
    if (row_height_ <= 0) return -1;
    int row = (y - padding_) / row_height_;
    return (0 <= row && row < static_cast<int>(page_.size())) ? row : -1;
}

void CandidateWindow::paint(HDC dc) {
    RECT client;
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    HGDIOBJ old = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    for (size_t i = 0; i < page_.size(); ++i) {
        RECT row = {client.left, padding_ + static_cast<int>(i) * row_height_, client.right,
                    padding_ + static_cast<int>(i + 1) * row_height_};
        bool selected = i == selected_;
        if (selected) FillRect(dc, &row, GetSysColorBrush(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        RECT text = row;
        text.left += padding_ * 2;
        DrawTextW(dc, page_[i].c_str(), static_cast<int>(page_[i].size()), &text,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    if (page_count_ > 1) {
        wchar_t footer[32];
        swprintf_s(footer, L"%zu/%zu", page_index_ + 1, page_count_);
        RECT text = {client.left, padding_ + static_cast<int>(page_.size()) * row_height_,
                     client.right - padding_ * 2, client.bottom - padding_};
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(dc, footer, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }
    SelectObject(dc, old);
}

LRESULT CALLBACK CandidateWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* created = static_cast<CandidateWindow*>(cs->lpCreateParams);
        created->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
    }
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        try {
            return self->handle(msg, wparam, lparam);
        } catch (...) {
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CandidateWindow::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONUP: {
            int row = row_at(GET_Y_LPARAM(lparam));
            if (row >= 0 && on_click_) on_click_(static_cast<size_t>(row));
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            if (on_wheel_) on_wheel_(delta < 0 ? 1 : -1);
            return 0;
        }
        case WM_DPICHANGED:
            update_font();
            return 0;
        default:
            return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

}  // namespace hiragana::tsf
