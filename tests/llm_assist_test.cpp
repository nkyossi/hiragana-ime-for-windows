// Checks that LlamaAssist reproduces LanguageModel.assist() and
// assist_yougen() of ibus-hiragana/engine/llm.py, recorded by
// tests/golden/gen_llm_assist_golden.py.
//
//   llm_assist_test <model.onnx> <vocab.txt> <mecab-dic-dir> <dic-dir> <golden.txt>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "BertTokenizer.h"
#include "LlamaAssist.h"
#include "OnnxBert.h"
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
    if (argc < 6) {
        std::cerr << "usage: llm_assist_test <model.onnx> <vocab.txt> <mecab-dic-dir> <dic-dir> <golden.txt>\n";
        return 2;
    }
    std::ifstream in(argv[5]);
    if (!in) {
        std::cerr << "cannot open " << argv[5] << "\n";
        return 2;
    }
    auto model = std::make_shared<OnnxBert>(argv[1]);
    auto tokenizer = std::make_shared<BertTokenizer>(argv[2], argv[3]);
    LlamaAssist assist(model, tokenizer, argv[4]);

    int cases = 0;
    int failures = 0;
    std::string line;
    while (getline_lf(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> fields = split(line, '\t');
        // <kind> <prefix> <yomi> <words or stems> <index=value ...>
        if (fields.size() != 5) continue;
        const ustring prefix = from_utf8(unescape(fields[1]));
        const ustring yomi = from_utf8(unescape(fields[2]));
        const std::vector<ustring> words = parse_list(fields[3]);
        const std::map<index_t, double> got =
            fields[0] == "A" ? assist.assist(prefix, yomi, words)
                             : assist.assist_yougen(prefix, yomi, words);
        std::map<index_t, double> expected;
        for (const std::string& pair : split(fields[4], ' ')) {
            if (pair.empty()) continue;
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) continue;
            expected[static_cast<index_t>(std::stoll(pair.substr(0, equals)))] =
                std::stod(pair.substr(equals + 1));
        }
        ++cases;
        if (expected.size() != got.size()) {
            std::cerr << "case " << cases << ": size " << got.size() << " != " << expected.size()
                      << "\n";
            ++failures;
            continue;
        }
        for (const auto& [index, value] : expected) {
            auto it = got.find(index);
            if (it == got.end() || !close(it->second, value)) {
                std::cerr << "case " << cases << ": index " << index << " got "
                          << (it == got.end() ? -1.0 : it->second) << " expected " << value << "\n";
                ++failures;
                break;
            }
        }
    }
    std::cout << cases << " cases, " << failures << " failures\n";
    return failures == 0 && 0 < cases ? 0 : 1;
}
