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

#include <ctfutb.h>
#include <msctf.h>

#include <functional>

namespace hiragana::tsf {

// The input mode indicator in the taskbar (GUID_LBI_INPUTMODE).
class LangBarButton : public ITfLangBarItemButton, public ITfSource {
public:
    struct Callbacks {
        std::function<char32_t()> mode;      // the current input mode character
        std::function<void()> toggle;        // left click
        std::function<void(POINT)> menu;     // right click
    };

    explicit LangBarButton(Callbacks callbacks);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
    STDMETHODIMP GetStatus(DWORD* status) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* area) override;
    STDMETHODIMP InitMenu(ITfMenu* menu) override;
    STDMETHODIMP OnMenuSelect(UINT id) override;
    STDMETHODIMP GetIcon(HICON* icon) override;
    STDMETHODIMP GetText(BSTR* text) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* cookie) override;
    STDMETHODIMP UnadviseSink(DWORD cookie) override;

    // Tells the language bar that the mode has changed.
    void update();

private:
    ~LangBarButton();

    LONG ref_ = 1;
    Callbacks callbacks_;
    ITfLangBarItemSink* sink_ = nullptr;
};

// Draws the mode character as an icon.
HICON create_mode_icon(char32_t mode);

}  // namespace hiragana::tsf
