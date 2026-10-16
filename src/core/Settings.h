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

// settings.json replaces the gsettings schema
// org.freedesktop.ibus.engine.hiragana of ibus-hiragana.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace hiragana {

struct Settings {
    std::string mode = "A";  // the input mode when a text field is first used
    std::string dictionary = "restrained.9.dic";
    std::string user_dictionary = "my.dic";
    std::string layout = "roomazi";
    std::string altgr = "altgr";
    std::string keyboard = "auto";  // "auto", "us", "jp" or "us+dvorak"
    bool permissible = false;
    bool use_half_width_digits = false;
    bool nn_as_jis_x_4063 = false;
    bool combining_circumflex = false;
    bool combining_macron = false;
    // LLM-assisted conversion (settings keys use-llm and use-cuda of the original).
    bool use_llm = false;
    bool use_cuda = false;
    // Pairs of keys (keysym names: space, Henkan, Muhenkan, Hiragana_Katakana)
    // that trade places while the input mode is not alphanumeric, e.g.
    // {"space", "Henkan"} to convert with the space bar of a 109 keyboard.
    std::vector<std::pair<std::string, std::string>> key_swaps;
    std::string logging_level = "WARNING";
    // Incremented by the setup program after it has cleared the input history.
    long long history_generation = 0;

    // Returns the defaults if the file is missing or broken.
    static Settings load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    bool same_dictionary(const Settings& other) const {
        return dictionary == other.dictionary && user_dictionary == other.user_dictionary &&
               permissible == other.permissible && history_generation == other.history_generation;
    }
    bool same_layout(const Settings& other) const {
        return layout == other.layout && altgr == other.altgr && keyboard == other.keyboard;
    }
};

// The initial content of a new user dictionary (USER_DICTIONARY_COMMENT in setup/main.py), UTF-8.
const char* user_dictionary_template();

}  // namespace hiragana
