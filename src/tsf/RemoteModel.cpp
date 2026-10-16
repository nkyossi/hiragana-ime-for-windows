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

#include "RemoteModel.h"

#include <utility>

#include "LlmProtocol.h"
#include "Log.h"

namespace hiragana {

namespace {

// A conversion waits for the model at most this long.
constexpr DWORD kTimeout = 3000;
// How often to try to connect while the server is not there.
constexpr DWORD kRetryInterval = 1000;
constexpr DWORD kRestartInterval = 30 * 1000;

bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    bool result = GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size) &&
                  elevation.TokenIsElevated;
    CloseHandle(token);
    return result;
}

}  // namespace

RemoteModel::RemoteModel(std::filesystem::path server) : server_(std::move(server)) {
    event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

RemoteModel::~RemoteModel() {
    disconnect();
    if (event_) CloseHandle(event_);
}

void RemoteModel::disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool RemoteModel::ready() {
    if (pipe_ != INVALID_HANDLE_VALUE) return true;
    DWORD now = GetTickCount();
    if (attempted_ && now - last_attempt_ < kRetryInterval) return false;
    attempted_ = true;
    last_attempt_ = now;
    std::wstring name = llm::pipe_name();
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        // ERROR_PIPE_BUSY: all instances are taken for the moment.
        if (error == ERROR_FILE_NOT_FOUND) start_server();
        LOG_DEBUG("LLM server not available: %lu", error);
        return false;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        CloseHandle(pipe);
        return false;
    }
    pipe_ = pipe;
    LOG_INFO("connected to the LLM server");
    return true;
}

void RemoteModel::start_server() {
    DWORD now = GetTickCount();
    if (started_ && now - last_start_ < kRestartInterval) return;
    started_ = true;
    last_start_ = now;
    // Do not start the server with the rights of an elevated app.
    if (is_elevated()) return;
    std::error_code ec;
    if (!std::filesystem::exists(server_, ec)) return;
    std::wstring command = L"\"" + server_.wstring() + L"\"";
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    // Leave the job of the app (if any) so that the server outlives it.
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
    BOOL ok = CreateProcessW(server_.c_str(), command.data(), nullptr, nullptr, FALSE,
                             flags | CREATE_BREAKAWAY_FROM_JOB, nullptr, server_.parent_path().c_str(), &si, &pi);
    if (!ok)
        ok = CreateProcessW(server_.c_str(), command.data(), nullptr, nullptr, FALSE, flags, nullptr,
                            server_.parent_path().c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        LOG_INFO("started the LLM server");
    } else {
        LOG_DEBUG("could not start the LLM server: %lu", GetLastError());
    }
}

bool RemoteModel::transact(const std::string& request, std::string& response) {
    auto wait = [this](OVERLAPPED& ov, DWORD& bytes) -> bool {
        if (WaitForSingleObject(event_, kTimeout) != WAIT_OBJECT_0) {
            CancelIoEx(pipe_, &ov);
            GetOverlappedResult(pipe_, &ov, &bytes, TRUE);
            LOG_WARNING("the LLM server did not answer in time");
            return false;
        }
        return GetOverlappedResult(pipe_, &ov, &bytes, FALSE) != FALSE || GetLastError() == ERROR_MORE_DATA;
    };
    OVERLAPPED ov = {};
    ov.hEvent = event_;
    ResetEvent(event_);
    DWORD bytes = 0;
    if (!WriteFile(pipe_, request.data(), static_cast<DWORD>(request.size()), nullptr, &ov) &&
        GetLastError() != ERROR_IO_PENDING)
        return false;
    if (!wait(ov, bytes)) return false;
    response.clear();
    char buffer[16384];
    for (;;) {
        ov = {};
        ov.hEvent = event_;
        ResetEvent(event_);
        bytes = 0;
        BOOL ok = ReadFile(pipe_, buffer, sizeof buffer, nullptr, &ov);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && error != ERROR_IO_PENDING && error != ERROR_MORE_DATA) return false;
        if (!wait(ov, bytes)) return false;
        bool more = !GetOverlappedResult(pipe_, &ov, &bytes, FALSE) && GetLastError() == ERROR_MORE_DATA;
        response.append(buffer, bytes);
        if (llm::kMaxMessage < response.size()) return false;
        if (!more) return true;
    }
}

std::map<index_t, double> RemoteModel::call(uint32_t op, const ustring& prefix, const ustring& yomi,
                                            const std::vector<ustring>& words) {
    std::map<index_t, double> result;
    if (!ready()) return result;
    std::string response;
    if (!transact(llm::encode_request(op, prefix, yomi, words), response) ||
        !llm::decode_response(response, result)) {
        disconnect();
        result.clear();
    }
    return result;
}

std::map<index_t, double> RemoteModel::assist(const ustring& prefix, const ustring& yomi,
                                              const std::vector<ustring>& words) {
    return call(llm::kAssist, prefix, yomi, words);
}

std::map<index_t, double> RemoteModel::assist_yougen(const ustring& prefix, const ustring& yomi,
                                                     const std::vector<ustring>& words) {
    return call(llm::kAssistYougen, prefix, yomi, words);
}

}  // namespace hiragana
