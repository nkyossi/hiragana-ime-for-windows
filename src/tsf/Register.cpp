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

// COM server and TSF profile registration (regsvr32).

#include "Register.h"

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <string>

#include "Globals.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;

namespace hiragana::tsf {

namespace {

const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_SECUREMODE,
    &GUID_TFCAT_TIPCAP_COMLESS,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

std::wstring clsid_key() {
    wchar_t clsid[64];
    StringFromGUID2(CLSID_TextService, clsid, static_cast<int>(std::size(clsid)));
    return std::wstring(L"CLSID\\") + clsid;
}

bool set_value(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

}  // namespace

bool register_server() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsid_key().c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok = set_value(key, nullptr, kAppName);
    HKEY inproc = nullptr;
    if (ok && RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                              &inproc, nullptr) == ERROR_SUCCESS) {
        ok = set_value(inproc, nullptr, module_path()) && set_value(inproc, L"ThreadingModel", L"Apartment");
        RegCloseKey(inproc);
    } else {
        ok = false;
    }
    RegCloseKey(key);
    return ok;
}

void unregister_server() { RegDeleteTreeW(HKEY_CLASSES_ROOT, clsid_key().c_str()); }

bool register_profile() {
    ComPtr<ITfInputProcessorProfileMgr> mgr;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&mgr))))
        return false;
    std::wstring icon = module_path();
    HRESULT hr = mgr->RegisterProfile(CLSID_TextService, kLangId, GUID_Profile, kDescription,
                                      static_cast<ULONG>(wcslen(kDescription)), icon.c_str(),
                                      static_cast<ULONG>(icon.size()), static_cast<ULONG>(-IDI_HIRAGANA), nullptr, 0,
                                      TRUE, 0);
    return SUCCEEDED(hr);
}

void unregister_profile() {
    ComPtr<ITfInputProcessorProfileMgr> mgr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&mgr)))) {
        mgr->UnregisterProfile(CLSID_TextService, kLangId, GUID_Profile, 0);
    }
}

bool register_categories() {
    ComPtr<ITfCategoryMgr> mgr;
    if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mgr))))
        return false;
    for (const GUID* category : kCategories) {
        if (FAILED(mgr->RegisterCategory(CLSID_TextService, *category, CLSID_TextService))) return false;
    }
    return true;
}

void unregister_categories() {
    ComPtr<ITfCategoryMgr> mgr;
    if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mgr)))) return;
    for (const GUID* category : kCategories) mgr->UnregisterCategory(CLSID_TextService, *category, CLSID_TextService);
}

}  // namespace hiragana::tsf
