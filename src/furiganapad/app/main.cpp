// FuriganaPad for Windows
//
// Copyright (c) 2026 Hiragana IME for Windows contributors
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, see <http://www.gnu.org/licenses/>.

#include <windows.h>

#include <commctrl.h>
#include <msctf.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<wchar_t*> args;
    for (int i = 0; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--activate-hiragana-ime") {
            // For tests: use Hiragana IME in this process.
            constexpr CLSID clsid = {0x1c841c5a, 0x1337, 0x4e4e, {0xbc, 0x44, 0x5b, 0x0e, 0xa1, 0x76, 0x90, 0x18}};
            constexpr GUID profile = {0xc27534cd, 0x9a44, 0x4271, {0xbd, 0xde, 0xe9, 0xfc, 0x24, 0x6a, 0x1d, 0xd5}};
            Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> mgr;
            if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&mgr))))
                mgr->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN),
                                     clsid, profile, nullptr,
                                     TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
            continue;
        }
        args.push_back(argv[i]);
    }
    int result = 0;
    {
        furiganapad::App app;
        result = app.run(static_cast<int>(args.size()), args.data());
    }
    LocalFree(argv);
    CoUninitialize();
    return result;
}
