// Checks that BertTokenizer reproduces the reference BertJapaneseTokenizer of
// tohoku-nlp/bert-base-japanese-v3 recorded by
// tests/golden/gen_llm_tokenizer_golden.py.
//
//   bert_tokenizer_test <vocab.txt> <mecab-dic-dir> <golden.txt>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "BertTokenizer.h"
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

std::vector<ustring> split_tokens(const std::string& s) {
    std::vector<ustring> tokens;
    for (const std::string& part : split(s, static_cast<char>(kUnitSeparator)))
        tokens.push_back(from_utf8(part));
    return tokens;
}

// "\u001f" joined tokens, the same representation the golden uses.
ustring join_tokens(const std::vector<ustring>& tokens) {
    ustring joined;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (0 < i) joined.push_back(kUnitSeparator);
        joined += tokens[i];
    }
    return joined;
}

std::vector<int64_t> parse_ids(const std::string& s) {
    std::vector<int64_t> ids;
    std::istringstream in(s);
    int64_t value = 0;
    while (in >> value) ids.push_back(value);
    return ids;
}

std::string to_utf8_ids(const std::vector<int64_t>& ids) {
    std::ostringstream out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (0 < i) out << ' ';
        out << ids[i];
    }
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: bert_tokenizer_test <vocab.txt> <mecab-dic-dir> <golden.txt>\n";
        return 2;
    }
    std::ifstream in(argv[3]);
    if (!in) {
        std::cerr << "cannot open " << argv[3] << "\n";
        return 2;
    }
    BertTokenizer tokenizer(argv[1], argv[2]);

    int cases = 0;
    int failures = 0;
    std::string line;
    while (getline_lf(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> fields = split(line, '\t');
        if (fields[0] == "T" && fields.size() == 4) {
            const ustring text = from_utf8(unescape(fields[1]));
            const ustring expected_tokens = join_tokens(split_tokens(fields[2]));
            const std::vector<int64_t> expected_ids = parse_ids(fields[3]);
            const ustring got_tokens = join_tokens(tokenizer.tokenize(text));
            const std::vector<int64_t> got_ids = tokenizer.encode(text);
            if (got_tokens != expected_tokens) {
                std::cerr << "case " << cases << ": tokens of \"" << to_utf8(text) << "\"\n  expected "
                          << to_utf8(expected_tokens) << "\n  got      " << to_utf8(got_tokens) << "\n";
                ++failures;
            } else if (got_ids != expected_ids) {
                std::cerr << "case " << cases << ": ids of \"" << to_utf8(text) << "\"\n  expected "
                          << to_utf8_ids(expected_ids) << "\n  got      " << to_utf8_ids(got_ids) << "\n";
                ++failures;
            }
            ++cases;
        } else if (fields[0] == "B" && 3 <= fields.size()) {
            std::vector<ustring> batch;
            for (size_t i = 1; i + 1 < fields.size(); ++i) batch.push_back(from_utf8(unescape(fields[i])));
            const std::vector<std::string> rows = split(fields.back(), static_cast<char>(kUnitSeparator));
            const std::vector<std::vector<int64_t>> got = tokenizer.encode_batch(batch);
            bool ok = got.size() == rows.size();
            for (size_t i = 0; ok && i < got.size(); ++i) {
                if (to_utf8_ids(got[i]) != rows[i]) {
                    std::cerr << "case " << cases << ": batch row " << i << "\n  expected " << rows[i]
                              << "\n  got      " << to_utf8_ids(got[i]) << "\n";
                    ok = false;
                }
            }
            if (!ok) ++failures;
            ++cases;
        }
    }
    std::cout << cases << " cases, " << failures << " failures\n";
    return failures == 0 && 0 < cases ? 0 : 1;
}
