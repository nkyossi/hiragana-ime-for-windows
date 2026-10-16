// Compares Dictionary::assisted_lookup() with the results of the original
// Python implementation written by tests/golden/gen_llm_golden.py.
//
//   assisted_golden_test <dic-dir> <system.dic> <romazi|kana> <golden.tsv>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "Dictionary.h"
#include "Model.h"
#include "Text.h"

using namespace hiragana;

namespace {

// The same deterministic stand-in used by tests/golden/gen_llm_golden.py.
class FakeModel : public Model {
public:
    static constexpr uint64_t kMod = 1000003;

    static uint64_t score(const ustring& prefix, const ustring& word) {
        uint64_t h = 0;
        for (char32_t ch : prefix) h = (h * 31 + static_cast<uint32_t>(ch)) % kMod;
        h = (h * 31) % kMod;  // separator
        for (char32_t ch : word) h = (h * 31 + static_cast<uint32_t>(ch)) % kMod;
        return h;
    }

    std::map<index_t, double> probs(const ustring& prefix, const std::vector<ustring>& words) {
        std::map<index_t, double> p;
        for (size_t i = 0; i < words.size(); ++i)
            p[static_cast<index_t>(i)] = static_cast<double>(score(prefix, words[i])) / static_cast<double>(kMod);
        return p;
    }

    std::map<index_t, double> assist(const ustring& prefix, const ustring&, const std::vector<ustring>& words) override {
        return probs(prefix, words);
    }

    std::map<index_t, double> assist_yougen(const ustring& prefix, const ustring&,
                                            const std::vector<ustring>& words) override {
        return probs(prefix, words);
    }

    std::string device_type() const override { return "cpu"; }
};

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
        std::cerr << "usage: assisted_golden_test <dic-dir> <system.dic> <romazi|kana> <golden.tsv>\n";
        return 2;
    }
    namespace fs = std::filesystem;
    fs::path user_dir = fs::temp_directory_path() / ("hiragana-assisted-" + std::to_string(std::rand()));
    fs::create_directories(user_dir);

    Dictionary::Config config;
    config.system_dir = fs::u8path(argv[1]);
    config.user_dir = user_dir;
    config.system = argv[2];
    config.user = "";
    Dictionary dict(config);
    dict.use_romazi(std::string(argv[3]) == "romazi");
    FakeModel model;

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
        if (f.size() != 7) continue;
        ++cases;
        ustring text = from_utf8(f[0]);
        index_t pos = static_cast<index_t>(std::stoll(f[1]));
        index_t anchor = static_cast<index_t>(std::stoll(f[2]));
        index_t suggested = 0;
        ustring current = dict.assisted_lookup(model, text, pos, anchor, suggested);
        std::string reading = current.empty() ? "" : to_utf8(dict.reading());
        std::string cand = current.empty() ? "" : join(dict.cand());
        std::string suggested_str = std::to_string(suggested);
        std::string complete = dict.is_complete() ? "1" : "0";
        if (reading != f[3] || cand != f[4] || suggested_str != f[5] || complete != f[6]) {
            if (++failures <= 20) {
                std::cerr << "MISMATCH text=" << f[0] << " pos=" << pos << " anchor=" << anchor << "\n"
                          << "  expected: " << f[3] << " | " << f[4] << " | " << f[5] << " | " << f[6] << "\n"
                          << "  actual:   " << reading << " | " << cand << " | " << suggested_str << " | "
                          << complete << "\n";
            }
        }
    }
    std::error_code ec;
    fs::remove_all(user_dir, ec);
    std::cout << cases << " cases, " << failures << " failures\n";
    return (failures == 0 && 0 < cases) ? 0 : 1;
}
