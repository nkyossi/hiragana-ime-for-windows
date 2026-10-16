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

#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace hiragana::log {

namespace {

Level g_level = WARNING;
std::mutex g_mutex;

Sink& sink() {
    static Sink s;
    return s;
}

}  // namespace

void set_level(Level level) { g_level = level; }

Level level() { return g_level; }

void set_sink(Sink s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    sink() = std::move(s);
}

Level level_from_name(const std::string& name) {
    if (name == "DEBUG") return DEBUG;
    if (name == "INFO") return INFO;
    if (name == "WARNING") return WARNING;
    if (name == "ERROR") return ERROR_;
    if (name == "CRITICAL") return CRITICAL;
    return WARNING;
}

const char* level_name(Level level) {
    switch (level) {
        case DEBUG: return "DEBUG";
        case INFO: return "INFO";
        case WARNING: return "WARNING";
        case ERROR_: return "ERROR";
        default: return "CRITICAL";
    }
}

void write(Level level, const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (sink()) sink()(level, buffer);
}

}  // namespace hiragana::log
