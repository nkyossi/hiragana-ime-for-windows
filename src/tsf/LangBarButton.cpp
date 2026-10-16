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

#include "LangBarButton.h"

#include <olectl.h>

#include <string>
#include <vector>

#include "Globals.h"

namespace hiragana::tsf {

namespace {

const DWORD kSinkCookie = 0x48495241;  // only one sink is supported

// The language bar item shown as the input mode in the taskbar (not in the SDK headers).
// {2C77A81E-41CC-4178-A3A7-5F8A987568E6}
constexpr GUID GUID_LBI_INPUTMODE = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};

std::wstring mode_name(char32_t mode) {
    switch (mode) {
        case U'あ': return L"ひらがな (あ)";
        case U'ア': return L"カタカナ (ア)";
        case U'Ａ': return L"全角英数 (Ａ)";
        case U'ｱ': return L"半角カタカナ (ｱ)";
        default: return L"英数 (A)";
    }
}

bool taskbar_uses_light_theme() {
    DWORD value = 0;
    DWORD size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        return value != 0;
    return false;
}

}  // namespace

HICON create_mode_icon(char32_t mode) {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;
    // Draw at twice the size for a smoother glyph; Windows scales it down.
    int n = size * 2;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = n;
    bi.bmiHeader.biHeight = -n;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !bits) {
        DeleteDC(dc);
        return nullptr;
    }
    HGDIOBJ old_bitmap = SelectObject(dc, color);

    // White glyph on black; the brightness becomes the alpha channel below.
    RECT rc = {0, 0, n, n};
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    LOGFONTW lf = {};
    lf.lfHeight = -(n * 7 / 8);
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Yu Gothic UI");
    HFONT font = CreateFontIndirectW(&lf);
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    wchar_t text[2] = {static_cast<wchar_t>(mode), 0};
    DrawTextW(dc, text, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(font);
    GdiFlush();

    BYTE fg = taskbar_uses_light_theme() ? 0 : 255;
    auto* px = static_cast<BYTE*>(bits);
    for (int i = 0; i < n * n; ++i) {
        BYTE alpha = px[i * 4 + 1];  // green channel of the white glyph
        // Premultiplied color with the alpha.
        BYTE c = static_cast<BYTE>(fg * alpha / 255);
        px[i * 4 + 0] = c;
        px[i * 4 + 1] = c;
        px[i * 4 + 2] = c;
        px[i * 4 + 3] = alpha;
    }
    SelectObject(dc, old_bitmap);
    DeleteDC(dc);

    std::vector<BYTE> mask_bits(static_cast<size_t>((n + 15) / 16 * 2 * n), 0);
    HBITMAP mask = CreateBitmap(n, n, 1, 1, mask_bits.data());
    ICONINFO ii = {TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

LangBarButton::LangBarButton(Callbacks callbacks) : callbacks_(std::move(callbacks)) { DllAddRef(); }

LangBarButton::~LangBarButton() {
    if (sink_) sink_->Release();
    DllRelease();
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfLangBarItem || riid == IID_ITfLangBarItemButton) {
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    } else if (riid == IID_ITfSource) {
        *ppv = static_cast<ITfSource*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() { return InterlockedIncrement(&ref_); }

STDMETHODIMP_(ULONG) LangBarButton::Release() {
    LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return n;
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
    if (!info) return E_INVALIDARG;
    info->clsidService = CLSID_TextService;
    info->guidItem = GUID_LBI_INPUTMODE;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    info->ulSort = 0;
    wcscpy_s(info->szDescription, kDescription);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* status) {
    if (!status) return E_INVALIDARG;
    *status = 0;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL) { return E_NOTIMPL; }

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* tooltip) {
    if (!tooltip) return E_INVALIDARG;
    std::wstring s = std::wstring(kDescription) + L" - " + mode_name(callbacks_.mode ? callbacks_.mode() : U'A');
    *tooltip = SysAllocString(s.c_str());
    return *tooltip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT pt, const RECT*) {
    try {
        if (click == TF_LBI_CLK_LEFT) {
            if (callbacks_.toggle) callbacks_.toggle();
        } else if (click == TF_LBI_CLK_RIGHT) {
            if (callbacks_.menu) callbacks_.menu(pt);
        }
    } catch (...) {
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu*) { return S_OK; }

STDMETHODIMP LangBarButton::OnMenuSelect(UINT) { return S_OK; }

STDMETHODIMP LangBarButton::GetIcon(HICON* icon) {
    if (!icon) return E_INVALIDARG;
    *icon = create_mode_icon(callbacks_.mode ? callbacks_.mode() : U'A');
    return *icon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* text) {
    if (!text) return E_INVALIDARG;
    *text = SysAllocString(mode_name(callbacks_.mode ? callbacks_.mode() : U'A').c_str());
    return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* cookie) {
    if (!punk || !cookie) return E_INVALIDARG;
    if (riid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&sink_)))) {
        sink_ = nullptr;
        return E_NOINTERFACE;
    }
    *cookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie) {
    if (cookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
    sink_->Release();
    sink_ = nullptr;
    return S_OK;
}

void LangBarButton::update() {
    if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

}  // namespace hiragana::tsf
