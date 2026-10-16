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

#include <windows.h>

#include <new>

#include "CandidateWindow.h"
#include "Globals.h"
#include "Register.h"
#include "TextService.h"

using namespace hiragana::tsf;

namespace {

class ClassFactory : public IClassFactory {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        DllAddRef();
        return 2;
    }
    STDMETHODIMP_(ULONG) Release() override {
        DllRelease();
        return 1;
    }

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            auto* service = new (std::nothrow) TextService();
            if (!service) return E_OUTOFMEMORY;
            HRESULT hr = service->QueryInterface(riid, ppv);
            service->Release();
            return hr;
        } catch (...) {
            return E_FAIL;
        }
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock)
            DllAddRef();
        else
            DllRelease();
        return S_OK;
    }
};

ClassFactory g_factory;

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        // FreeLibrary(): a later load must not find our class with a stale window procedure.
        CandidateWindow::unregister_class();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (rclsid != CLSID_TextService) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return DllRefCount() == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    if (!register_server() || !register_profile() || !register_categories()) {
        unregister_categories();
        unregister_profile();
        unregister_server();
        return E_FAIL;
    }
    return S_OK;
}

STDAPI DllUnregisterServer() {
    unregister_categories();
    unregister_profile();
    unregister_server();
    return S_OK;
}
