// Checks that nfkc() reproduces Python's unicodedata.normalize('NFKC', text)
// recorded by tests/golden/gen_nfkc_golden.py.
//
//   nfkc_test <golden.txt>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Nfkc.h"
#include "Text.h"

using namespace hiragana;

namespace {

std::istream& getline_lf(std::istream& in, std::string& line) {
    if (!std::getline(in, line)) return in;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return in;
}

ustring parse_hexes(const std::string& s) {
    ustring out;
    std::istringstream in(s);
    unsigned value = 0;
    while (in >> std::hex >> value) out.push_back(static_cast<char32_t>(value));
    return out;
}

std::string to_hexes(const ustring& s) {
    std::ostringstream out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (0 < i) out << ' ';
        out << std::hex << std::uppercase << static_cast<unsigned>(s[i]);
    }
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: nfkc_test <golden.txt>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }
    int cases = 0;
    int failures = 0;
    std::string line;
    while (getline_lf(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const ustring input = parse_hexes(line.substr(0, tab));
        const ustring expected = parse_hexes(line.substr(tab + 1));
        const ustring got = nfkc(input);
        if (got != expected) {
            std::cerr << "case " << cases << ": " << to_hexes(input) << "\n  expected " << to_hexes(expected)
                      << "\n  got      " << to_hexes(got) << "\n";
            ++failures;
        }
        ++cases;
    }
    std::cout << cases << " cases, " << failures << " failures\n";
    return failures == 0 && 0 < cases ? 0 : 1;
}
