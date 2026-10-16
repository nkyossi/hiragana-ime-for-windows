// Hiragana IME for Windows - LLM server
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

// hiragana-llm.exe loads the language model of the LLM-assisted conversion
// (LlamaAssist) once and answers the text services through a named pipe; see
// LlmProtocol.h. The text service starts it when use-llm is on. It exits when
// use-llm is turned off, when use-cuda changes, or after it has been idle for
// a while.

#include <windows.h>

#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "LlamaAssist.h"
#include "LlmProtocol.h"
#include "Log.h"
#include "Settings.h"

namespace fs = std::filesystem;
using namespace hiragana;

namespace {

constexpr DWORD kIdleTimeout = 30 * 60 * 1000;

std::mutex g_model_mutex;
std::shared_ptr<Model> g_model;
std::atomic<DWORD> g_last_request{0};

fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

fs::path user_data_dir() { return known_folder(FOLDERID_RoamingAppData) / L"HiraganaIME"; }
fs::path settings_path() {
    // For tests
    wchar_t buffer[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"HIRAGANA_LLM_SETTINGS", buffer, MAX_PATH);
    if (0 < n && n < MAX_PATH) return fs::path(std::wstring(buffer, n));
    return user_data_dir() / L"settings.json";
}

fs::path exe_dir() {
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return fs::path(std::wstring(buffer, n)).parent_path();
}

void init_logging(const Settings& settings) {
    log::set_level(log::level_from_name(settings.logging_level));
    fs::path file = user_data_dir() / L"hiragana-llm.log";
    static std::mutex mutex;
    log::set_sink([file](log::Level level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &now);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
        std::ofstream out(file, std::ios::app | std::ios::binary);
        out << stamp << ' ' << log::level_name(level) << ' ' << message << '\n';
    });
}

// The directories that may hold model.onnx, vocab.txt and unidic\.
fs::path find_model_dir() {
    for (const fs::path& dir : {known_folder(FOLDERID_LocalAppData) / L"HiraganaIME" / L"llm", exe_dir() / L"llm"}) {
        std::error_code ec;
        if (fs::exists(dir / L"model.onnx", ec)) return dir;
    }
    return known_folder(FOLDERID_LocalAppData) / L"HiraganaIME" / L"llm";
}

std::shared_ptr<Model> load_model(const Settings& settings) {
    fs::path dir = find_model_dir();
    fs::path data = exe_dir() / L"data";
    int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u));
    std::string error;
    DWORD start = GetTickCount();
    auto model = LlamaAssist::create(true, settings.use_cuda ? "cuda" : "cpu", dir / L"model.onnx",
                                     dir / L"vocab.txt", dir / L"unidic", data, threads, &error);
    if (model)
        LOG_INFO("model loaded from %s on %s in %lu ms", dir.u8string().c_str(), model->device_type().c_str(),
                 static_cast<unsigned long>(GetTickCount() - start));
    else
        LOG_ERROR("could not load the model: %s", error.c_str());
    return model;
}

std::string handle_request(const std::string& request) {
    llm::Reader r(request);
    uint32_t version = r.u32();
    uint32_t op = r.u32();
    ustring prefix = r.str();
    ustring yomi = r.str();
    uint32_t n = r.u32();
    std::vector<ustring> words;
    for (uint32_t i = 0; r.ok() && i < n; ++i) words.push_back(r.str());
    if (!r.ok() || version != llm::kVersion) return llm::encode_response(1, {});
    try {
        std::lock_guard<std::mutex> lock(g_model_mutex);
        DWORD start = GetTickCount();
        std::map<index_t, double> result = op == llm::kAssistYougen ? g_model->assist_yougen(prefix, yomi, words)
                                                                    : g_model->assist(prefix, yomi, words);
        LOG_DEBUG("%s \"%s\" %zu words: %lu ms", op == llm::kAssistYougen ? "assist_yougen" : "assist",
                  to_utf8(yomi).c_str(), words.size(), static_cast<unsigned long>(GetTickCount() - start));
        return llm::encode_response(0, result);
    } catch (const std::exception& e) {
        LOG_ERROR("request failed: %s", e.what());
        return llm::encode_response(1, {});
    }
}

void serve(HANDLE pipe) {
    std::string request;
    char buffer[16384];
    for (;;) {
        DWORD read = 0;
        BOOL ok = ReadFile(pipe, buffer, sizeof buffer, &read, nullptr);
        if (!ok && GetLastError() != ERROR_MORE_DATA) break;
        request.append(buffer, read);
        if (llm::kMaxMessage < request.size()) break;
        if (!ok) continue;  // the rest of the message follows
        g_last_request = GetTickCount();
        std::string response = handle_request(request);
        request.clear();
        DWORD written = 0;
        if (!WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr)) break;
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

// The user, SYSTEM and the apps in AppContainers (Store apps, the search
// box) may connect; AppContainers run at low integrity.
bool make_security(SECURITY_ATTRIBUTES& sa) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    char buffer[256];
    DWORD size = 0;
    bool ok = GetTokenInformation(token, TokenUser, buffer, sizeof buffer, &size);
    CloseHandle(token);
    if (!ok) return false;
    LPWSTR user = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, &user)) return false;
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(user) +
                        L")(A;;GRGW;;;S-1-15-2-1)(A;;GRGW;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
    LocalFree(user);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
        return false;
    sa = {sizeof sa, sd, FALSE};
    return true;
}

void watch(const Settings& started) {
    fs::path path = settings_path();
    for (;;) {
        Sleep(15 * 1000);
        Settings now = Settings::load(path);
        if (!now.use_llm || now.use_cuda != started.use_cuda) {
            LOG_INFO("exiting: the settings have changed");
            ExitProcess(0);
        }
        if (kIdleTimeout < GetTickCount() - g_last_request) {
            LOG_INFO("exiting: idle");
            ExitProcess(0);
        }
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, llm::mutex_name().c_str());
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

    Settings settings = Settings::load(settings_path());
    init_logging(settings);
    if (!settings.use_llm) return 0;
    g_model = load_model(settings);
    if (!g_model) return 1;
    g_last_request = GetTickCount();
    std::thread(watch, settings).detach();

    SECURITY_ATTRIBUTES sa = {};
    if (!make_security(sa)) {
        LOG_ERROR("could not make the security descriptor: %lu", GetLastError());
        return 1;
    }
    std::wstring name = llm::pipe_name();
    DWORD first = FILE_FLAG_FIRST_PIPE_INSTANCE;
    for (;;) {
        HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | first,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                                           PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            LOG_ERROR("CreateNamedPipe failed: %lu", GetLastError());
            return 1;
        }
        first = 0;
        if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            std::thread(serve, pipe).detach();
        } else {
            CloseHandle(pipe);
        }
    }
}
