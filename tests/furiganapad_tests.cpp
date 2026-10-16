// Tests of the FuriganaPad core, and of the furigana added when the
// Hiragana IME engine drives it like IBus drives the original FuriganaPad.

#include <cstring>
#include <filesystem>
#include <random>

#include "Annotation.h"
#include "Engine.h"
#include "InputContext.h"
#include "Keysyms.h"
#include "Markdown.h"
#include "RubyExport.h"
#include "Test.h"
#include "TextBuffer.h"

namespace fs = std::filesystem;
using furiganapad::FuriganaBuffer;
using furiganapad::InputContext;
using furiganapad::TextIter;
using furiganapad::IAA;
using furiganapad::IAS;
using furiganapad::IAT;
using hiragana::ustring;

namespace {

ustring ruby(const ustring& base, const ustring& reading) {
    return ustring(1, IAA) + base + ustring(1, IAS) + reading + ustring(1, IAT);
}

ustring all_text(FuriganaBuffer& b) {
    auto [s, e] = b.get_bounds();
    return b.get_text(s, e, true);
}

// The engine's client as the GTK IM module of the original FuriganaPad.
class FuriganaClient : public hiragana::Client {
public:
    explicit FuriganaClient(InputContext& input) : input_(input) {}
    void get_surrounding_text(ustring& text, hiragana::index_t& pos) override {
        auto [t, p] = input_.retrieve_surrounding();
        text = t;
        pos = p;
    }
    void delete_surrounding_text(hiragana::index_t size) override { input_.delete_surrounding(-size, size); }
    void commit_text(const ustring& text) override { input_.ibus_commit_text(text); }
    void update_preedit(const hiragana::Preedit& p) override {
        furiganapad::Preedit preedit;
        preedit.text = p.text();
        preedit.cursor = static_cast<furiganapad::index_t>(preedit.text.size());
        input_.ibus_update_preedit_text(preedit);
    }
    void update_lookup_table(const hiragana::LookupTable&, bool) override {}

private:
    InputContext& input_;
};

uint32_t keycode_of(char c) {
    const char* rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const uint32_t starts[] = {16, 30, 44};
    for (int r = 0; r < 3; ++r) {
        const char* p = std::strchr(rows[r], c);
        if (p) return starts[r] + static_cast<uint32_t>(p - rows[r]);
    }
    return 0;
}

struct Pad {
    fs::path user_dir;
    FuriganaBuffer buffer;
    InputContext input{buffer};
    FuriganaClient client{input};
    std::unique_ptr<hiragana::Engine> engine;

    Pad() {
        user_dir = fs::temp_directory_path() / ("furiganapad-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(user_dir);
        hiragana::Settings s;
        s.mode = "あ";
        s.user_dictionary = "";
        engine = std::make_unique<hiragana::Engine>(hiragana::Engine::Paths{fs::u8path(UPSTREAM_DIR), user_dir}, s,
                                                    "us");
        engine->set_client(&client);
        engine->set_surrounding_supported(true);
    }
    ~Pad() {
        engine.reset();
        std::error_code ec;
        fs::remove_all(user_dir, ec);
    }
    void key(hiragana::keysym::Keysym keyval, uint32_t keycode, uint32_t state = 0) {
        using namespace hiragana::keysym;
        engine->process_key_event(keyval, keycode, state);
        engine->process_key_event(keyval, keycode, state | RELEASE_MASK);
    }
    void type(const char* s) {
        for (; *s; ++s) key(static_cast<hiragana::keysym::Keysym>(*s), keycode_of(*s));
    }
    void space(bool shift = false) {
        using namespace hiragana::keysym;
        if (shift) engine->process_key_event(Shift_L, 42, 0);
        key(space_key, 57, shift ? SHIFT_MASK : 0);
        if (shift) engine->process_key_event(Shift_L, 42, SHIFT_MASK | RELEASE_MASK);
    }
    void enter() { key(hiragana::keysym::Return, 28); }
    static constexpr hiragana::keysym::Keysym space_key = hiragana::keysym::space;
};

}  // namespace

TEST(fp_helpers) {
    using namespace furiganapad;
    EXPECT_EQ(get_plain_text(ruby(U"漢字", U"かんじ") + U"は"), ustring(U"漢字は"));
    EXPECT_TRUE(is_reading(U"かんじ"));
    EXPECT_TRUE(is_reading(U"うご―く"));
    EXPECT_TRUE(!is_reading(U"漢字"));
    // A dangling annotation at the start of a copied text keeps the base text only.
    ustring dangling = ustring(U"字") + ustring(1, IAS) + U"かんじ" + ustring(1, IAT) + U"です";
    EXPECT_EQ(remove_dangling_annotations(dangling), ustring(U"字です"));
    EXPECT_EQ(remove_dangling_annotations(ruby(U"漢字", U"かんじ")), ruby(U"漢字", U"かんじ"));
}

TEST(fp_cursor_positions_skip_rubies) {
    furiganapad::Paragraph p(ruby(U"漢字", U"かんじ") + U"は");
    // 0 IAA, 1 漢, 2 字, 3 IAS, 4-6 かんじ, 7 IAT, 8 は
    EXPECT_EQ(p.forward_cursor_position(0), static_cast<furiganapad::index_t>(2));
    EXPECT_EQ(p.forward_cursor_position(2), static_cast<furiganapad::index_t>(8));
    EXPECT_EQ(p.forward_cursor_position(8), static_cast<furiganapad::index_t>(9));
    EXPECT_EQ(p.backward_cursor_position(8), static_cast<furiganapad::index_t>(2));
    EXPECT_EQ(p.get_plain_offset(8), static_cast<furiganapad::index_t>(2));
    EXPECT_EQ(p.expand_plain_offset(2), static_cast<furiganapad::index_t>(8));
    EXPECT_EQ(p.rubies().size(), static_cast<size_t>(1));
}

TEST(fp_insert_delete_and_undo) {
    FuriganaBuffer b;
    b.begin_user_action();
    b.insert_at_cursor(U"一行目\n" + ruby(U"漢字", U"かんじ") + U"です");
    b.end_user_action();
    EXPECT_EQ(b.get_line_count(), static_cast<furiganapad::index_t>(2));
    // Deleting a part of the base text removes the ruby but keeps the rest of the base.
    TextIter start = b.get_iter_at_line_offset(1, 2);
    TextIter end = b.get_iter_at_line_offset(1, 8);
    b.begin_user_action();
    b.delete_(start, end);
    b.end_user_action();
    EXPECT_EQ(b.paragraph(1).get_text(), ustring(U"漢です"));
    b.do_undo();
    b.do_undo();
    EXPECT_EQ(b.paragraph(1).get_text(), ruby(U"漢字", U"かんじ") + U"です");
    b.do_redo();
    b.do_redo();
    EXPECT_EQ(b.paragraph(1).get_text(), ustring(U"漢です"));
}

TEST(fp_unconvert) {
    FuriganaBuffer b;
    b.insert_at_cursor(ruby(U"漢字", U"かんじ"));
    EXPECT_TRUE(b.unconvert(b.get_cursor()));
    EXPECT_EQ(all_text(b), ustring(U"かんじ"));
}

TEST(fp_markdown_conversion) {
    using namespace furiganapad;
    ustring annotated = U"# " + ruby(U"見出", U"みだ") + U"し\n" + ruby(U"漢字", U"かんじ");
    ustring md = annotated_to_markdown(annotated);
    EXPECT_EQ(md, ustring(U"# <ruby>見出<rp>(</rp><rt>みだ</rt><rp>)</rp></ruby>し\n"
                          U"<ruby>漢字<rp>(</rp><rt>かんじ</rt><rp>)</rp></ruby>"));
    EXPECT_EQ(markdown_to_annotated(md), annotated);
    EXPECT_EQ(markdown_to_annotated(U"<ruby>漢字<rt>かんじ</rt></ruby>"), ruby(U"漢字", U"かんじ"));
    EXPECT_EQ(markdown_to_annotated(U"<ruby><rb>漢</rb><rt>かん</rt><rb>字</rb><rt>じ</rt></ruby>"),
              ruby(U"漢", U"かん") + ruby(U"字", U"じ"));
    EXPECT_EQ(markdown_to_annotated(U"a <ruby>broken"), ustring(U"a <ruby>broken"));
}

TEST(fp_copy_with_ruby) {
    using namespace furiganapad;
    ustring text = ruby(U"今日", U"きょう") + U"は<" + ruby(U"漢字", U"かんじ") + U">&\n2行め";
    EXPECT_EQ(to_note_ruby(text), ustring(U"|今日《きょう》は<|漢字《かんじ》>&\n2行め"));
    EXPECT_EQ(to_html_ruby(text),
              ustring(U"<ruby>今日<rp>(</rp><rt>きょう</rt><rp>)</rp></ruby>は&lt;"
                      U"<ruby>漢字<rp>(</rp><rt>かんじ</rt><rp>)</rp></ruby>&gt;&amp;<br>\n2行め"));
    // A broken annotation is copied without its marks.
    EXPECT_EQ(to_note_ruby(ustring(1, IAA) + U"漢字" + ustring(1, IAS) + U"かん"), ustring(U"漢字かん"));

    // CF_HTML: the offsets point at the fragment.
    std::string fragment = "<ruby>漢<rt>かん</rt></ruby>";
    std::string data = make_cf_html(fragment);
    auto offset = [&](const char* key) {
        size_t at = data.find(key);
        return at == std::string::npos ? std::string::npos : std::stoul(data.substr(at + std::strlen(key), 10));
    };
    EXPECT_EQ(data.substr(offset("StartFragment:"), offset("EndFragment:") - offset("StartFragment:")), fragment);
    EXPECT_EQ(data.substr(offset("StartHTML:"), 6), std::string("<html>"));
    EXPECT_EQ(offset("EndHTML:"), data.size());
}

TEST(fp_markdown_styles) {
    using namespace furiganapad;
    auto styles = markdown_styles({U"## 見出し", U"**太字**と`コード`", U"- 項目", U"```", U"# no heading", U"```"});
    EXPECT_EQ(styles[0].heading, 2);
    EXPECT_EQ(styles[1].spans.size(), static_cast<size_t>(6));
    EXPECT_EQ(static_cast<int>(styles[1].spans[1].kind), static_cast<int>(MarkdownSpan::Bold));
    EXPECT_TRUE(!styles[2].spans.empty());
    EXPECT_TRUE(styles[4].code_block);
    EXPECT_EQ(styles[4].heading, 0);
}

TEST(fp_ime_adds_furigana) {
    Pad pad;
    pad.type("kanji");
    EXPECT_EQ(all_text(pad.buffer), ustring(U"かんじ"));
    pad.space();
    EXPECT_EQ(all_text(pad.buffer), ustring(U""));
    EXPECT_EQ(pad.input.preedit().text, ustring(U"漢字"));
    pad.enter();
    EXPECT_EQ(all_text(pad.buffer), ruby(U"漢字", U"かんじ"));
    EXPECT_TRUE(!pad.input.has_preedit());
}

TEST(fp_ime_text_before_and_after) {
    Pad pad;
    pad.type("kyou");
    pad.space();
    pad.enter();
    pad.type("ha");
    pad.type("kanji");
    pad.space();
    pad.type("desu");
    EXPECT_EQ(all_text(pad.buffer), ruby(U"今日", U"きょう") + U"は" + ruby(U"漢字", U"かんじ") + U"です");
}

TEST(fp_ime_okurigana) {
    Pad pad;
    pad.type("ugo");
    pad.space(true);
    pad.type("ku");
    EXPECT_EQ(all_text(pad.buffer), ruby(U"動", U"うご") + U"く");
}

TEST(fp_ime_escape_and_undo) {
    Pad pad;
    pad.type("kanji");
    pad.space();
    pad.key(hiragana::keysym::Escape, 1);
    EXPECT_EQ(all_text(pad.buffer), ustring(U"かんじ"));
    pad.space();
    pad.enter();
    EXPECT_EQ(all_text(pad.buffer), ruby(U"漢字", U"かんじ"));
    // Undo takes back the conversion as one step, with the reading deleted by the IME.
    pad.buffer.do_undo();
    EXPECT_EQ(all_text(pad.buffer), ustring(U"かんじ"));
}

TEST(fp_ruby_mode_off) {
    Pad pad;
    pad.buffer.set_ruby_mode(false);
    pad.type("kanji");
    pad.space();
    pad.enter();
    EXPECT_EQ(all_text(pad.buffer), ustring(U"漢字"));
}
