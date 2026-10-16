// Checks hiragana-llm.exe through RemoteModel, the client of the text
// service, with the cases of tests/golden/gen_llm_assist_golden.py. It starts
// a server of its own (HIRAGANA_LLM_TEST) with use-llm on; the model files
// must be in %LOCALAPPDATA%\HiraganaIME\llm (installer/get-llm.ps1).
//
//   llm_remote_test <hiragana-llm.exe> <golden.txt> [<dic-dir>]

#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Dictionary.h"
#include "RemoteModel.h"
#include "Settings.h"
#include "Text.h"

using namespace hiragana;

namespace {

const char32_t kUnitSeparator = 0x1F;

std::istream& getline_lf(std::istream& in, std::string& line) {
    if (!std::getline(in, line)) return in;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return in;
}

std::vector<std::string> split(const std::string& s, char separator) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(separator, start);
        if (pos == std::string::npos) {
            fields.push_back(s.substr(start));
            break;
        }
        fields.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            if (c == '\\')
                out.push_back('\\');
            else if (c == 't')
                out.push_back('\t');
            else if (c == 'n')
                out.push_back('\n');
            else {
                out.push_back('\\');
                out.push_back(c);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<ustring> parse_list(const std::string& field) {
    std::vector<ustring> values;
    for (const std::string& part : split(field, static_cast<char>(kUnitSeparator)))
        values.push_back(from_utf8(unescape(part)));
    return values;
}

bool close(double a, double b) { return std::fabs(a - b) <= 1e-6 + 1e-4 * std::fabs(b); }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: llm_remote_test <hiragana-llm.exe> <golden.txt>\n";
        return 2;
    }
    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot open " << argv[2] << "\n";
        return 2;
    }
    std::wstring id = std::to_wstring(GetCurrentProcessId());
    SetEnvironmentVariableW(L"HIRAGANA_LLM_TEST", id.c_str());
    std::filesystem::path settings_file = std::filesystem::temp_directory_path() / (L"llm_remote_test_" + id + L".json");
    Settings settings;
    settings.use_llm = true;
    settings.logging_level = "DEBUG";
    settings.save(settings_file);
    SetEnvironmentVariableW(L"HIRAGANA_LLM_SETTINGS", settings_file.c_str());

    RemoteModel remote(std::filesystem::u8path(argv[1]));
    auto start = std::chrono::steady_clock::now();
    while (!remote.ready()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(120)) {
            std::cerr << "the server did not start\n";
            return 1;
        }
        Sleep(200);
    }
    std::cout << "server ready in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
              << " ms\n";

    int cases = 0;
    int failures = 0;
    double total_ms = 0;
    double max_ms = 0;
    std::string line;
    while (getline_lf(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() != 5) continue;
        const ustring prefix = from_utf8(unescape(fields[1]));
        const ustring yomi = from_utf8(unescape(fields[2]));
        const std::vector<ustring> words = parse_list(fields[3]);
        auto t0 = std::chrono::steady_clock::now();
        const std::map<index_t, double> got =
            fields[0] == "A" ? remote.assist(prefix, yomi, words) : remote.assist_yougen(prefix, yomi, words);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        total_ms += ms;
        if (max_ms < ms) max_ms = ms;
        std::map<index_t, double> expected;
        for (const std::string& pair : split(fields[4], ' ')) {
            const size_t equals = pair.find('=');
            if (pair.empty() || equals == std::string::npos) continue;
            expected[static_cast<index_t>(std::stoll(pair.substr(0, equals)))] = std::stod(pair.substr(equals + 1));
        }
        ++cases;
        bool ok = expected.size() == got.size();
        for (const auto& [index, value] : expected) {
            auto it = got.find(index);
            if (it == got.end() || !close(it->second, value)) ok = false;
        }
        if (!ok) {
            std::cerr << "case " << cases << " differs\n";
            ++failures;
        }
    }
    std::cout << cases << " cases, " << failures << " failures; " << (cases ? total_ms / cases : 0)
              << " ms on average, " << max_ms << " ms at most\n";

    // Dictionary::assisted_lookup() through the server, against
    // dictionary.py assisted_lookup() with the real model.
    if (3 < argc) {
        std::filesystem::path user_dir = std::filesystem::temp_directory_path() / (L"llm_remote_test_dic_" + id);
        std::filesystem::create_directories(user_dir);
        Dictionary::Config config;
        config.system_dir = std::filesystem::u8path(argv[3]);
        config.user_dir = user_dir;
        config.user = "";
        Dictionary dict(config);
        struct Case {
            const char32_t* text;
            const char32_t* cand;
            index_t cursor;
        };
        const Case lookups[] = {
            {U"かんじ", U"漢字", 0},         {U"きょう", U"今日", 2},   {U"きょうはかんじ", U"漢字", 0},
            {U"がっこう", U"学校", 0},       {U"きしゃ", U"記者", 0},
        };
        for (const Case& c : lookups) {
            ustring text = c.text;
            index_t cursor = -1;
            ustring cand = dict.assisted_lookup(remote, text, len(text), 0, cursor);
            bool ok = cand == c.cand && cursor == c.cursor;
            std::cout << (ok ? "PASS " : "FAIL ") << to_utf8(text) << " -> " << to_utf8(cand) << " " << cursor
                      << " (" << to_utf8(dict.reading()) << ", " << dict.cand().size() << " candidates)\n";
            if (!ok) ++failures;
            dict.reset();
        }
        std::error_code ec;
        std::filesystem::remove_all(user_dir, ec);
    }

    // Turning use-llm off stops the server.
    settings.use_llm = false;
    settings.save(settings_file);
    remote.disconnect();
    return failures == 0 && 0 < cases ? 0 : 1;
}
