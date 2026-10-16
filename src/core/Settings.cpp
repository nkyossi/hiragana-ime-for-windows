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

#include "Settings.h"

#include <fstream>
#include <random>
#include <sstream>

#include "Json.h"
#include "Log.h"

namespace hiragana {

namespace fs = std::filesystem;

const char* user_dictionary_template() {
    return u8"; ひらがな IME ユーザ辞書\r\n"
           u8";\r\n"
           u8"; セミコロン(;)ではじまる行はコメントです。\r\n"
           u8"; 単語は、「よみ」, 半角スペース, そのあとにスラッシュ(/)で語をかこんで登録します。\r\n"
           u8";\r\n"
           u8";   例) きれい /綺麗/\r\n"
           u8";\r\n"
           u8"; くわしくは、ヘルプの「設定のかえかた」-「辞書タブ」をみてください。\r\n"
           u8";\r\n"
           u8"\r\n";
}

Settings Settings::load(const fs::path& path) {
    Settings s;
    std::ifstream f(path, std::ios::binary);
    if (!f) return s;
    std::ostringstream ss;
    ss << f.rdbuf();
    json::Value v;
    try {
        v = json::parse(ss.str());
    } catch (const std::exception& e) {
        LOG_ERROR("could not load \"%s\": %s", path.u8string().c_str(), e.what());
        return s;
    }
    auto str = [&](const char* key, std::string& out) {
        if (v[key].is_string()) out = v[key].as_string();
    };
    auto boolean = [&](const char* key, bool& out) {
        if (v[key].is_bool()) out = v[key].as_bool();
    };
    str("mode", s.mode);
    str("dictionary", s.dictionary);
    str("user-dictionary", s.user_dictionary);
    str("layout", s.layout);
    str("altgr", s.altgr);
    str("keyboard", s.keyboard);
    boolean("permissible", s.permissible);
    boolean("use-half-width-digits", s.use_half_width_digits);
    boolean("nn-as-jis-x-4063", s.nn_as_jis_x_4063);
    boolean("combining-circumflex", s.combining_circumflex);
    boolean("combining-macron", s.combining_macron);
    boolean("use-llm", s.use_llm);
    boolean("use-cuda", s.use_cuda);
    str("logging-level", s.logging_level);
    if (v["history-generation"].is_number())
        s.history_generation = static_cast<long long>(v["history-generation"].as_number());
    return s;
}

bool Settings::save(const fs::path& path) const {
    json::Value v(json::Value::Object{});
    v.set("mode", mode);
    v.set("dictionary", dictionary);
    v.set("user-dictionary", user_dictionary);
    v.set("layout", layout);
    v.set("altgr", altgr);
    v.set("keyboard", keyboard);
    v.set("permissible", permissible);
    v.set("use-half-width-digits", use_half_width_digits);
    v.set("nn-as-jis-x-4063", nn_as_jis_x_4063);
    v.set("combining-circumflex", combining_circumflex);
    v.set("combining-macron", combining_macron);
    v.set("use-llm", use_llm);
    v.set("use-cuda", use_cuda);
    v.set("logging-level", logging_level);
    v.set("history-generation", history_generation);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += "." + std::to_string(std::random_device{}()) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << v.dump();
        if (!f) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace hiragana
