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

#include "Globals.h"

#include <shlobj.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

#include "Log.h"

namespace hiragana::tsf {

namespace fs = std::filesystem;

HINSTANCE g_module = nullptr;

namespace {
LONG g_ref_count = 0;
}

void DllAddRef() { InterlockedIncrement(&g_ref_count); }
void DllRelease() { InterlockedDecrement(&g_ref_count); }
LONG DllRefCount() { return g_ref_count; }

std::wstring module_path() {
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(g_module, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::wstring(buffer, n);
}

fs::path module_dir() {
    fs::path dir = fs::path(module_path()).parent_path();
    // The 32-bit DLL lives in an "x86" subdirectory next to the data files.
    if (!fs::exists(dir / "dic") && fs::exists(dir.parent_path() / "dic")) return dir.parent_path();
    return dir;
}

fs::path user_data_dir() {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &path))) {
        result = fs::path(path) / L"HiraganaIME";
    }
    CoTaskMemFree(path);
    return result;
}

fs::path settings_path() { return user_data_dir() / L"settings.json"; }

void init_logging() {
    static std::once_flag once;
    std::call_once(once, [] {
        fs::path file = user_data_dir() / L"hiragana-ime.log";
        log::set_sink([file](log::Level level, const std::string& message) {
            std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_s(&tm, &now);
            char stamp[32];
            std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
            char header[96];
            std::snprintf(header, sizeof header, "%s:%s:%lu:", stamp, log::level_name(level), GetCurrentProcessId());
            std::string line = std::string(header) + message + "\n";
            OutputDebugStringA(line.c_str());
            std::ofstream f(file, std::ios::binary | std::ios::app);
            if (f) f << line;
        });
    });
}

}  // namespace hiragana::tsf
