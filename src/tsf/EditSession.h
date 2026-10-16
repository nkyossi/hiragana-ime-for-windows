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

#include <msctf.h>

#include <functional>

#include "Globals.h"

namespace hiragana::tsf {

// An ITfEditSession that runs a function with the edit cookie.
class EditSession : public ITfEditSession {
public:
    explicit EditSession(std::function<HRESULT(TfEditCookie)> fn) : fn_(std::move(fn)) { DllAddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return n;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        try {
            return fn_(ec);
        } catch (...) {
            return E_FAIL;
        }
    }

private:
    ~EditSession() { DllRelease(); }

    LONG ref_ = 1;
    std::function<HRESULT(TfEditCookie)> fn_;
};

}  // namespace hiragana::tsf
