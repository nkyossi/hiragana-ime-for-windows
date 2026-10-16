// Compares Dictionary::lookup() with the results of the original Python
// implementation written by tests/golden/gen_golden.py.
//
//   golden_test <dic-dir> <system.dic> <romazi|kana> <golden.tsv>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Dictionary.h"
#include "Text.h"

using namespace hiragana;

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (;;) {
        size_t pos = line.find('\t', start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

std::string join(const std::vector<ustring>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += '/';
        s += to_utf8(v[i]);
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: golden_test <dic-dir> <system.dic> <romazi|kana> <golden.tsv>\n";
        return 2;
    }
    namespace fs = std::filesystem;
    fs::path user_dir = fs::temp_directory_path() / ("hiragana-golden-" + std::to_string(std::rand()));
    fs::create_directories(user_dir);

    Dictionary::Config config;
    config.system_dir = fs::u8path(argv[1]);
    config.user_dir = user_dir;
    config.system = argv[2];
    config.user = "";
    Dictionary dict(config);
    dict.use_romazi(std::string(argv[3]) == "romazi");

    std::ifstream in(fs::u8path(argv[4]), std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << argv[4] << "\n";
        return 2;
    }
    std::string line;
    index_t cases = 0;
    index_t failures = 0;
    while (std::getline(in, line)) {
        auto f = split_tabs(line);
        if (f.size() != 6) continue;
        ++cases;
        ustring text = from_utf8(f[0]);
        index_t pos = static_cast<index_t>(std::stoll(f[1]));
        index_t anchor = static_cast<index_t>(std::stoll(f[2]));
        ustring current = dict.lookup(text, pos, anchor);
        std::string reading = current.empty() ? "" : to_utf8(dict.reading());
        std::string cand = current.empty() ? "" : join(dict.cand());
        std::string complete = dict.is_complete() ? "1" : "0";
        if (reading != f[3] || cand != f[4] || complete != f[5]) {
            if (++failures <= 20) {
                std::cerr << "MISMATCH text=" << f[0] << " pos=" << pos << " anchor=" << anchor << "\n"
                          << "  expected: " << f[3] << " | " << f[4] << " | " << f[5] << "\n"
                          << "  actual:   " << reading << " | " << cand << " | " << complete << "\n";
            }
        }
    }
    std::error_code ec;
    fs::remove_all(user_dir, ec);
    std::cout << cases << " cases, " << failures << " failures\n";
    return (failures == 0 && 0 < cases) ? 0 : 1;
}
