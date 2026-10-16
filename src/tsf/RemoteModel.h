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

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Model.h"

namespace hiragana {

// A Model that asks hiragana-llm.exe (see LlmProtocol.h).
class RemoteModel : public Model {
public:
    explicit RemoteModel(std::filesystem::path server);
    ~RemoteModel() override;
    RemoteModel(const RemoteModel&) = delete;
    RemoteModel& operator=(const RemoteModel&) = delete;

    // Returns true if the server is connected. Otherwise it tries to connect,
    // starting the server if it is not running; the server needs a few
    // seconds to load the model, so the conversion goes without it meanwhile.
    bool ready();
    void disconnect();

    std::map<index_t, double> assist(const ustring& prefix, const ustring& yomi,
                                     const std::vector<ustring>& words) override;
    std::map<index_t, double> assist_yougen(const ustring& prefix, const ustring& yomi,
                                            const std::vector<ustring>& words) override;
    std::string device_type() const override { return "remote"; }

private:
    std::map<index_t, double> call(uint32_t op, const ustring& prefix, const ustring& yomi,
                                   const std::vector<ustring>& words);
    bool transact(const std::string& request, std::string& response);
    void start_server();

    std::filesystem::path server_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE event_ = nullptr;
    DWORD last_attempt_ = 0;
    bool attempted_ = false;
    DWORD last_start_ = 0;
    bool started_ = false;
};

}  // namespace hiragana
