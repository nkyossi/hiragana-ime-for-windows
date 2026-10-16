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

#include "DisplayAttributes.h"

#include <new>

#include "Globals.h"

namespace hiragana::tsf {

namespace {

struct AttributeDef {
    const GUID* guid;
    const wchar_t* description;
    TF_DISPLAYATTRIBUTE attr;
};

// CANDIDATE_BACKGROUND_COLOR = 0xd1eaff and PREFIX_LOCK_COLOR = 0x3399ff in engine.py
const AttributeDef kAttributes[] = {
    {&GUID_DisplayAttributeInput, L"Hiragana IME input",
     {{TF_CT_NONE, {0}}, {TF_CT_NONE, {0}}, TF_LS_DOT, FALSE, {TF_CT_NONE, {0}}, TF_ATTR_INPUT}},
    {&GUID_DisplayAttributeCandidate, L"Hiragana IME candidate",
     {{TF_CT_COLORREF, {RGB(0x00, 0x00, 0x00)}},
      {TF_CT_COLORREF, {RGB(0xd1, 0xea, 0xff)}},
      TF_LS_SOLID,
      FALSE,
      {TF_CT_NONE, {0}},
      TF_ATTR_TARGET_CONVERTED}},
    {&GUID_DisplayAttributeLocked, L"Hiragana IME prefix lock",
     {{TF_CT_COLORREF, {RGB(0x33, 0x99, 0xff)}}, {TF_CT_NONE, {0}}, TF_LS_NONE, FALSE, {TF_CT_NONE, {0}},
      TF_ATTR_INPUT}},
};

class DisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    explicit DisplayAttributeInfo(const AttributeDef& def) : def_(def) { DllAddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
            *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
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

    STDMETHODIMP GetGUID(GUID* guid) override {
        if (!guid) return E_INVALIDARG;
        *guid = *def_.guid;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR* desc) override {
        if (!desc) return E_INVALIDARG;
        *desc = SysAllocString(def_.description);
        return *desc ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* attr) override {
        if (!attr) return E_INVALIDARG;
        *attr = def_.attr;
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override { return S_OK; }

private:
    ~DisplayAttributeInfo() { DllRelease(); }

    LONG ref_ = 1;
    const AttributeDef& def_;
};

class EnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
public:
    explicit EnumDisplayAttributeInfo(size_t index = 0) : index_(index) { DllAddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) {
            *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
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

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** e) override {
        if (!e) return E_INVALIDARG;
        *e = new (std::nothrow) EnumDisplayAttributeInfo(index_);
        return *e ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** infos, ULONG* fetched) override {
        if (!infos) return E_INVALIDARG;
        ULONG n = 0;
        while (n < count && index_ < std::size(kAttributes)) {
            infos[n] = new (std::nothrow) DisplayAttributeInfo(kAttributes[index_]);
            if (!infos[n]) break;
            ++n;
            ++index_;
        }
        if (fetched) *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG count) override {
        index_ = (std::min)(index_ + static_cast<size_t>(count), std::size(kAttributes));
        return index_ < std::size(kAttributes) ? S_OK : S_FALSE;
    }

private:
    ~EnumDisplayAttributeInfo() { DllRelease(); }

    LONG ref_ = 1;
    size_t index_;
};

}  // namespace

const GUID& attribute_guid(Attribute a) { return *kAttributes[static_cast<size_t>(a)].guid; }

HRESULT get_display_attribute_info(REFGUID guid, ITfDisplayAttributeInfo** info) {
    if (!info) return E_INVALIDARG;
    *info = nullptr;
    for (const auto& def : kAttributes) {
        if (*def.guid == guid) {
            *info = new (std::nothrow) DisplayAttributeInfo(def);
            return *info ? S_OK : E_OUTOFMEMORY;
        }
    }
    return E_INVALIDARG;
}

HRESULT enum_display_attribute_info(IEnumTfDisplayAttributeInfo** e) {
    if (!e) return E_INVALIDARG;
    *e = new (std::nothrow) EnumDisplayAttributeInfo();
    return *e ? S_OK : E_OUTOFMEMORY;
}

}  // namespace hiragana::tsf
