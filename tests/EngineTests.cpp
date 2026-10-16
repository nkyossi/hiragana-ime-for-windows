// Engine tests for the parts that are new in the Windows port. The ported
// behavior itself is compared with the original by engine_replay.

#include <cstring>
#include <filesystem>
#include <random>
#include <string>

#include "Engine.h"
#include "Keysyms.h"
#include "Test.h"

using namespace hiragana;
using namespace hiragana::keysym;
namespace fs = std::filesystem;

namespace {

class DocumentClient : public Client {
public:
    ustring document;
    index_t cursor = 0;
    ustring preedit;
    bool table_visible = false;
    Mode last_mode = 0;

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
    void mode_changed(Mode mode) override { last_mode = mode; }
};

// evdev key codes of a US keyboard for lowercase letters
uint32_t keycode_of(char c) {
    const char* rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const uint32_t starts[] = {16, 30, 44};
    for (int r = 0; r < 3; ++r) {
        const char* p = std::strchr(rows[r], c);
        if (p) return starts[r] + static_cast<uint32_t>(p - rows[r]);
    }
    return 0;
}

struct Fixture {
    fs::path user_dir;
    DocumentClient client;
    std::unique_ptr<Engine> engine;

    explicit Fixture(bool surrounding = true, const std::string& mode = "あ") {
        user_dir = fs::temp_directory_path() / ("hiragana-engine-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(user_dir);
        Settings s;
        s.mode = mode;
        s.user_dictionary = "";
        engine = std::make_unique<Engine>(Engine::Paths{fs::u8path(UPSTREAM_DIR), user_dir}, s, "us");
        engine->set_client(&client);
        engine->set_surrounding_supported(surrounding);
    }
    ~Fixture() {
        engine.reset();
        std::error_code ec;
        fs::remove_all(user_dir, ec);
    }

    void key(Keysym keyval, uint32_t keycode, uint32_t state = 0) {
        engine->process_key_event(keyval, keycode, state);
        engine->process_key_event(keyval, keycode, state | RELEASE_MASK);
    }
    void type(const char* s) {
        for (; *s; ++s) key(static_cast<Keysym>(*s), keycode_of(*s));
    }
};

}  // namespace

TEST(engine_converts_kanji) {
    Fixture f;
    f.type("kanji");
    EXPECT_EQ(f.client.document, ustring(U"かんじ"));
    f.key(space, 57);
    EXPECT_EQ(f.client.document, ustring(U""));
    EXPECT_EQ(f.client.preedit, ustring(U"漢字"));
    EXPECT_TRUE(f.client.table_visible);
    f.key(Return, 28);
    EXPECT_EQ(f.client.document, ustring(U"漢字"));
    EXPECT_EQ(f.client.preedit, ustring(U""));
}

TEST(engine_legacy_mode_keeps_text_in_preedit) {
    Fixture f(false);
    f.type("kanji");
    EXPECT_EQ(f.client.document, ustring(U""));
    EXPECT_EQ(f.client.preedit, ustring(U"かんじ"));
    f.key(space, 57);
    EXPECT_EQ(f.client.preedit, ustring(U"漢字"));
    f.key(Return, 28);
    f.key(Return, 28);
    EXPECT_EQ(f.client.document, ustring(U"漢字"));
}

TEST(engine_commit_all_keeps_the_candidate) {
    Fixture f;
    f.type("kanji");
    f.key(space, 57);
    f.engine->commit_all();
    EXPECT_EQ(f.client.document, ustring(U"漢字"));
    EXPECT_EQ(f.client.preedit, ustring(U""));
    EXPECT_TRUE(!f.engine->has_pending_input());

    f.type("k");
    f.engine->commit_all();
    EXPECT_EQ(f.client.document, ustring(U"漢字k"));
}

TEST(engine_composition_terminated_forgets_conversion) {
    Fixture f;
    f.type("kanji");
    f.key(space, 57);
    f.engine->composition_terminated();
    EXPECT_TRUE(!f.engine->has_pending_input());
    EXPECT_TRUE(!f.client.table_visible);
}

TEST(engine_mode_keys) {
    Fixture f(true, "A");
    EXPECT_TRUE(f.engine->is_trivial_key('a', 30, 0));
    EXPECT_TRUE(!f.engine->is_trivial_key(Zenkaku_Hankaku, 41, 0));
    f.key(Zenkaku_Hankaku, 41);
    EXPECT_EQ(f.engine->get_mode(), MODE_HIRAGANA);
    EXPECT_EQ(f.client.last_mode, MODE_HIRAGANA);
    EXPECT_TRUE(!f.engine->is_trivial_key('a', 30, 0));
    f.key(Muhenkan, 94);
    EXPECT_EQ(f.engine->get_mode(), MODE_ALPHANUMERIC);
}

TEST(engine_trivial_keys_match_full_processing) {
    // In the alphanumeric mode, a key that is_trivial_key() lets go must be
    // one that process_key_event() would not process either.
    Fixture f(true, "A");
    for (char c = 'a'; c <= 'z'; ++c) {
        EXPECT_TRUE(f.engine->is_trivial_key(static_cast<Keysym>(c), keycode_of(c), 0));
        EXPECT_TRUE(!f.engine->process_key_event(static_cast<Keysym>(c), keycode_of(c), 0));
    }
    EXPECT_EQ(f.client.document, ustring(U""));
}
