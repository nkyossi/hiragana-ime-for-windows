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

// The protocol between the text service and hiragana-llm.exe.
//
// The language model takes about 1 GB of memory, so it is not loaded into
// every process that uses the text service. hiragana-llm.exe (64-bit only)
// loads it once per session, and the text services (64-bit and 32-bit) ask it
// through a message-mode named pipe.
//
// Request:  u32 version, u32 op, str prefix, str yomi, u32 n, str words[n]
// Response: u32 status (0: ok), u32 n, {i64 index, f64 probability}[n]
// where u32/i64/f64 are little endian and str is a u32 byte count followed by
// UTF-8.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Text.h"

namespace hiragana::llm {

constexpr uint32_t kVersion = 1;
constexpr uint32_t kAssist = 1;
constexpr uint32_t kAssistYougen = 2;
constexpr DWORD kMaxMessage = 1 << 20;

// For tests: another server than the one of the session.
inline std::wstring test_suffix() {
    wchar_t buffer[64];
    DWORD n = GetEnvironmentVariableW(L"HIRAGANA_LLM_TEST", buffer, 64);
    return 0 < n && n < 64 ? L"-test-" + std::wstring(buffer, n) : std::wstring();
}

// One server per logon session.
inline std::wstring pipe_name() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return L"\\\\.\\pipe\\HiraganaIME-LLM-" + std::to_wstring(session) + test_suffix();
}

inline std::wstring mutex_name() { return L"Local\\HiraganaIME-LLM" + test_suffix(); }

class Writer {
public:
    void u32(uint32_t v) { raw(&v, sizeof v); }
    void i64(int64_t v) { raw(&v, sizeof v); }
    void f64(double v) { raw(&v, sizeof v); }
    void str(const ustring& s) {
        std::string utf8 = to_utf8(s);
        u32(static_cast<uint32_t>(utf8.size()));
        raw(utf8.data(), utf8.size());
    }
    const std::string& data() const { return data_; }

private:
    void raw(const void* p, std::size_t n) { data_.append(static_cast<const char*>(p), n); }
    std::string data_;
};

class Reader {
public:
    explicit Reader(const std::string& data) : data_(data) {}
    bool ok() const { return ok_; }
    uint32_t u32() { return value<uint32_t>(); }
    int64_t i64() { return value<int64_t>(); }
    double f64() { return value<double>(); }
    ustring str() {
        uint32_t n = u32();
        if (!ok_ || data_.size() - pos_ < n) {
            ok_ = false;
            return ustring();
        }
        ustring s = from_utf8(std::string_view(data_.data() + pos_, n));
        pos_ += n;
        return s;
    }

private:
    template <typename T>
    T value() {
        T v{};
        if (!ok_ || data_.size() - pos_ < sizeof v) {
            ok_ = false;
            return v;
        }
        std::memcpy(&v, data_.data() + pos_, sizeof v);
        pos_ += sizeof v;
        return v;
    }
    const std::string& data_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

inline std::string encode_request(uint32_t op, const ustring& prefix, const ustring& yomi,
                                  const std::vector<ustring>& words) {
    Writer w;
    w.u32(kVersion);
    w.u32(op);
    w.str(prefix);
    w.str(yomi);
    w.u32(static_cast<uint32_t>(words.size()));
    for (const ustring& word : words) w.str(word);
    return w.data();
}

inline std::string encode_response(uint32_t status, const std::map<index_t, double>& result) {
    Writer w;
    w.u32(status);
    w.u32(static_cast<uint32_t>(result.size()));
    for (const auto& [index, p] : result) {
        w.i64(static_cast<int64_t>(index));
        w.f64(p);
    }
    return w.data();
}

inline bool decode_response(const std::string& data, std::map<index_t, double>& result) {
    Reader r(data);
    uint32_t status = r.u32();
    uint32_t n = r.u32();
    if (!r.ok() || status != 0) return false;
    for (uint32_t i = 0; i < n; ++i) {
        int64_t index = r.i64();
        double p = r.f64();
        if (!r.ok()) return false;
        result[static_cast<index_t>(index)] = p;
    }
    return true;
}

}  // namespace hiragana::llm
