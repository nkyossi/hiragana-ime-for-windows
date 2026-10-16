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

// Minimal logging with the same levels as the Python logging module.
// Messages are UTF-8, printf-style.

#include <functional>
#include <string>

namespace hiragana::log {

enum Level { DEBUG = 10, INFO = 20, WARNING = 30, ERROR_ = 40, CRITICAL = 50 };

using Sink = std::function<void(Level, const std::string&)>;

void set_level(Level level);
Level level();
void set_sink(Sink sink);
// Parses "DEBUG", "INFO", ... ; returns WARNING for unknown names.
Level level_from_name(const std::string& name);
const char* level_name(Level level);

void write(Level level, const char* format, ...);

}  // namespace hiragana::log

#define HIRAGANA_LOG(lv, ...)                                                   \
    do {                                                                        \
        if (::hiragana::log::level() <= (lv)) ::hiragana::log::write((lv), __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG(...) HIRAGANA_LOG(::hiragana::log::DEBUG, __VA_ARGS__)
#define LOG_INFO(...) HIRAGANA_LOG(::hiragana::log::INFO, __VA_ARGS__)
#define LOG_WARNING(...) HIRAGANA_LOG(::hiragana::log::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) HIRAGANA_LOG(::hiragana::log::ERROR_, __VA_ARGS__)
