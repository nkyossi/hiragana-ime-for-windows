// Replays the key events recorded by tests/golden/gen_engine_golden.py with
// the C++ engine and compares the results with the original Python engine.
//
//   engine_replay <ibus-hiragana-dir> <engine-golden.txt>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Engine.h"
#include "Log.h"
#include "Model.h"
#include "Text.h"

using namespace hiragana;
namespace fs = std::filesystem;

namespace {

// The deterministic stand-in for the language model; the same one used by
// tests/golden/gen_engine_golden.py and tests/assisted_golden_test.cpp.
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

class DocumentClient : public Client {
public:
    ustring document;
    index_t cursor = 0;
    ustring preedit;
    bool table_visible = false;

    void get_surrounding_text(ustring& text, index_t& pos) override {
        text = document;
        pos = cursor;
    }
    void delete_surrounding_text(index_t size) override {
        index_t start = cursor - size;
        document = slice_to(document, start) + slice_from(document, cursor);
        cursor = start;
    }
    void commit_text(const ustring& text) override {
        document = slice_to(document, cursor) + text + slice_from(document, cursor);
        cursor += len(text);
    }
    void update_preedit(const Preedit& p) override { preedit = p.text(); }
    void update_lookup_table(const LookupTable&, bool visible) override { table_visible = visible; }
};

std::string snapshot(Engine& engine, DocumentClient& client, bool result) {
    std::string cands;
    const auto& table = engine.lookup_table();
    for (size_t i = 0; i < table.get_number_of_candidates(); ++i) {
        if (i) cands += '/';
        cands += to_utf8(table.get_candidate(i));
    }
    return std::string("R\t") + (result ? "1" : "0") + "\t" + to_utf8(client.document) + "\t" +
           std::to_string(client.cursor) + "\t" + to_utf8(client.preedit) + "\t" + (client.table_visible ? "1" : "0") +
           "\t" + cands + "\t" + std::to_string(table.get_cursor_pos()) + "\t" +
           to_utf8(ustring(1, engine.get_mode()));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: engine_replay <ibus-hiragana-dir> <engine-golden.txt>\n";
        return 2;
    }
    fs::path upstream = fs::u8path(argv[1]);
    fs::path user_dir = fs::temp_directory_path() / ("hiragana-replay-" + std::to_string(std::random_device{}()));
    fs::create_directories(user_dir);

    std::ifstream in(fs::u8path(argv[2]), std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << argv[2] << "\n";
        return 2;
    }

    std::unique_ptr<Engine> engine;
    std::unique_ptr<DocumentClient> client;
    std::string scenario;
    std::vector<std::string> history;
    bool failed_scenario = false;
    index_t events = 0;
    index_t failures = 0;
    index_t scenarios = 0;
    std::string line;
    std::vector<std::string> last_event;
    bool last_result = false;

    while (std::getline(in, line)) {
        auto f = split_tabs(line);
        if (f[0] == "S") {
            ++scenarios;
            scenario = f[1];
            history.clear();
            failed_scenario = false;
            Settings s;
            s.mode = f[5];
            s.dictionary = "restrained.9.dic";
            s.user_dictionary = "";
            s.layout = f[2];
            std::string options = f.size() > 6 ? f[6] : "";
            auto has = [&](const std::string& opt) { return options.find(opt + "=1") != std::string::npos; };
            s.use_half_width_digits = has("use-half-width-digits");
            s.nn_as_jis_x_4063 = has("nn-as-jis-x-4063");
            s.combining_circumflex = has("combining-circumflex");
            s.combining_macron = has("combining-macron");
            // Each scenario starts without input history, as the Python engine does.
            engine.reset();
            std::error_code ec;
            fs::remove_all(user_dir, ec);
            fs::create_directories(user_dir);
            client = std::make_unique<DocumentClient>();
            engine = std::make_unique<Engine>(Engine::Paths{upstream, user_dir}, s, f[3]);
            engine->set_client(client.get());
            engine->set_surrounding_supported(f[4] == "1");
            if (has("use-llm")) engine->set_model(std::make_shared<FakeModel>());
        } else if (f[0] == "E") {
            ++events;
            last_event = f;
            last_result = engine->process_key_event(static_cast<uint32_t>(std::stoul(f[1])),
                                                    static_cast<uint32_t>(std::stoul(f[2])),
                                                    static_cast<uint32_t>(std::stoul(f[3])));
            history.push_back(line);
        } else if (f[0] == "R") {
            std::string actual = snapshot(*engine, *client, last_result);
            history.push_back(line);
            if (actual != line && !failed_scenario) {
                failed_scenario = true;
                if (++failures <= 10) {
                    std::cerr << "MISMATCH in " << scenario << " after event " << last_event[1] << " "
                              << last_event[2] << " " << last_event[3] << "\n";
                    size_t from = history.size() > 12 ? history.size() - 12 : 0;
                    for (size_t i = from; i < history.size(); ++i) std::cerr << "    " << history[i] << "\n";
                    std::cerr << "  actual: " << actual << "\n";
                }
            }
        }
    }
    engine.reset();
    std::error_code ec;
    fs::remove_all(user_dir, ec);
    std::cout << scenarios << " scenarios, " << events << " events, " << failures << " failed scenarios\n";
    return (failures == 0 && 0 < events) ? 0 : 1;
}
