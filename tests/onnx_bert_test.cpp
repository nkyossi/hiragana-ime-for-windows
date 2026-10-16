// Checks that OnnxBert reproduces the reference onnxruntime output recorded by
// tests/golden/gen_onnx_bert_golden.py.
//
//   onnx_bert_test <model.onnx> <golden.txt>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "OnnxBert.h"

using namespace hiragana;

namespace {

std::istream& getline_lf(std::istream& in, std::string& line) {
    if (!std::getline(in, line)) return in;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return in;
}

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

std::vector<int64_t> parse_ids(const std::string& s) {
    std::vector<int64_t> ids;
    std::istringstream in(s);
    int64_t v = 0;
    while (in >> v) ids.push_back(v);
    return ids;
}

std::vector<double> parse_values(const std::string& s) {
    std::vector<double> values;
    std::istringstream in(s);
    double v = 0.0;
    while (in >> v) values.push_back(v);
    return values;
}

bool close(double a, double b) { return std::fabs(a - b) <= 1e-5 + 1e-4 * std::fabs(b); }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: onnx_bert_test <model.onnx> <golden.txt>\n";
        return 2;
    }
    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot open " << argv[2] << "\n";
        return 2;
    }
    OnnxBert model(argv[1]);
    std::cout << "vocab size " << model.vocab_size() << "\n";

    int cases = 0;
    int failures = 0;
    std::string line;
    std::vector<int64_t> ids;
    while (getline_lf(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto fields = split_tabs(line);
        if (fields.size() < 2) continue;
        if (fields[0] == "CASE") {
            ids = parse_ids(fields[1]);
        } else if (fields[0] == "SOFTMAX") {
            auto expected = parse_values(fields[1]);
            auto logits = model.run(ids);
            if (logits.size() != ids.size()) {
                std::cerr << "case " << cases << ": sequence " << logits.size() << " != "
                          << ids.size() << "\n";
                ++failures;
            } else {
                auto got = softmax(logits.back());
                if (got.size() != expected.size()) {
                    std::cerr << "case " << cases << ": vocab " << got.size() << " != "
                              << expected.size() << "\n";
                    ++failures;
                } else {
                    double worst = 0.0;
                    for (size_t i = 0; i < got.size(); ++i) {
                        if (!close(got[i], expected[i])) {
                            worst = std::max(worst, std::fabs(got[i] - expected[i]));
                        }
                    }
                    if (0.0 < worst) {
                        std::cerr << "case " << cases << ": max abs diff " << worst << "\n";
                        ++failures;
                    }
                }
            }
            ++cases;
        }
    }
    std::cout << cases << " cases, " << failures << " failures\n";
    return failures == 0 && 0 < cases ? 0 : 1;
}
