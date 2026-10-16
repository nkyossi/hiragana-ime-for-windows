// Unit tests for the core library. tests/test_dictionary.py of ibus-hiragana
// is ported in test_match().

#include <filesystem>
#include <fstream>
#include <random>

#include "Dictionary.h"
#include "Json.h"
#include "KanaTables.h"
#include "Settings.h"
#include "Test.h"
#include "Text.h"

using namespace hiragana;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("hiragana-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

Dictionary::Config config_for(const fs::path& user_dir, const std::string& system = "restrained.9.dic") {
    Dictionary::Config config;
    config.system_dir = fs::u8path(UPSTREAM_DIR) / "dic";
    config.user_dir = user_dir;
    config.system = system;
    config.user = "my.dic";
    return config;
}

}  // namespace

TEST(test_match) {
    TempDir dir;
    Dictionary dict(config_for(dir.path));
    dict.use_romazi(false);

    EXPECT_EQ(dict.match(U"", U"る"), -1);

    EXPECT_EQ(dict.match(U"k", U"く"), 0);
    EXPECT_EQ(dict.match(U"k", U"くと"), 1);
    EXPECT_EQ(dict.match(U"k", U"い"), 0);
    EXPECT_EQ(dict.match(U"k", U"いて"), 0);
    EXPECT_EQ(dict.match(U"k", U"いてと"), 1);
    EXPECT_EQ(dict.match(U"k", U"いで"), -1);
    EXPECT_EQ(dict.match(U"k", U"ぐ"), -1);
    EXPECT_EQ(dict.match(U"k", U"けは"), 0);
    EXPECT_EQ(dict.match(U"k", U"けば"), 0);
    EXPECT_EQ(dict.match(U"k", U"けばと"), 1);
    EXPECT_EQ(dict.match(U"k", U"す"), -1);

    EXPECT_EQ(dict.match(U"g", U"く"), 0);
    EXPECT_EQ(dict.match(U"g", U"いて"), 0);
    EXPECT_EQ(dict.match(U"g", U"いで"), 0);
    EXPECT_EQ(dict.match(U"g", U"いてい"), -1);
    EXPECT_EQ(dict.match(U"g", U"ぎの"), 1);

    EXPECT_EQ(dict.match(U"b", U"は"), 0);

    // 食べ
    EXPECT_EQ(dict.match(U"べ1", U""), 0);
    EXPECT_EQ(dict.match(U"べ1", U"へ"), 0);
    EXPECT_EQ(dict.match(U"べ1", U"べ"), 0);
    EXPECT_EQ(dict.match(U"べ1", U"べれ"), 0);
    EXPECT_EQ(dict.match(U"べ1", U"べあ"), 1);
    EXPECT_EQ(dict.match(U"べ1", U"え"), -1);

    // 速さ，早さ
    EXPECT_EQ(dict.match(U"i", U"さ"), 0);

    // 美しみ
    EXPECT_EQ(dict.match(U"しi", U"しみ"), 0);

    EXPECT_EQ(dict.match(U"いに", U"いにね"), 1);
    EXPECT_EQ(dict.match(U"いに", U"いに"), 0);
    EXPECT_EQ(dict.match(U"いに", U"い"), 0);
    EXPECT_EQ(dict.match(U"いに", U""), 0);

    // 涙ぐましい
    EXPECT_EQ(dict.match(U"ぐましi", U"く"), 0);
}

TEST(test_lookup_taigen_and_yougen) {
    TempDir dir;
    Dictionary dict(config_for(dir.path));
    ustring text = U"かんじ";
    EXPECT_EQ(dict.lookup(text, len(text)), ustring(U"漢字"));
    EXPECT_EQ(dict.reading(), ustring(U"かんじ"));
    text = U"うご―く";
    EXPECT_EQ(dict.lookup(text, len(text)), ustring(U"動く"));
    // _match('k', 'く') is 0: more okurigana may follow.
    EXPECT_TRUE(!dict.is_complete());
    text = U"うご―くと";
    EXPECT_EQ(dict.lookup(text, len(text)), ustring(U"動くと"));
    EXPECT_TRUE(dict.is_complete());
}

TEST(test_learning_is_saved) {
    TempDir dir;
    ustring text = U"かんじ";
    {
        Dictionary dict(config_for(dir.path));
        dict.lookup(text, len(text));
        EXPECT_EQ(dict.current(), ustring(U"漢字"));
        dict.set_current(1);
        ustring second = dict.current();
        dict.confirm(U"");
        dict.reset();
        EXPECT_EQ(dict.lookup(text, len(text)), second);
        dict.save_orders();
    }
    {
        Dictionary dict(config_for(dir.path));
        EXPECT_EQ(dict.lookup(text, len(text)), ustring(U"幹事"));
    }
}

TEST(test_history_is_merged_between_processes) {
    TempDir dir;
    Dictionary a(config_for(dir.path));
    Dictionary b(config_for(dir.path));
    ustring kanji = U"かんじ";
    ustring kisya = U"きしゃ";

    a.lookup(kanji, len(kanji));
    a.set_current(1);
    ustring a_choice = a.current();
    a.confirm(U"");
    a.reset();
    a.save_orders();

    b.lookup(kisya, len(kisya));
    b.set_current(2);
    ustring b_choice = b.current();
    b.confirm(U"");
    b.reset();
    b.save_orders();  // must keep the entry saved by a

    Dictionary c(config_for(dir.path));
    EXPECT_EQ(c.lookup(kanji, len(kanji)), a_choice);
    EXPECT_EQ(c.lookup(kisya, len(kisya)), b_choice);

    // a picks up what b has saved.
    a.sync();
    EXPECT_EQ(a.lookup(kisya, len(kisya)), b_choice);
}

TEST(test_clear_history) {
    TempDir dir;
    ustring text = U"かんじ";
    {
        Dictionary dict(config_for(dir.path));
        dict.lookup(text, len(text));
        dict.set_current(1);
        dict.confirm(U"");
        dict.reset();
        dict.save_orders();
    }
    auto config = config_for(dir.path);
    config.clear_history = true;
    Dictionary dict(config);
    EXPECT_EQ(dict.lookup(text, len(text)), ustring(U"漢字"));
}

TEST(test_user_dictionary) {
    TempDir dir;
    {
        std::ofstream f(dir.path / "my.dic", std::ios::binary);
        f << "; v1.0.0\nひらがなあいえむいー /平仮名IME/\n";
    }
    Dictionary dict(config_for(dir.path));
    ustring text = U"ひらがなあいえむいー";
    EXPECT_EQ(dict.lookup(text, len(text)), ustring(U"平仮名IME"));
}

TEST(test_add_katakana) {
    TempDir dir;
    Dictionary dict(config_for(dir.path));
    dict.add_katakana(U"ホゲホゲ");
    ustring text = U"ほげほげ";
    EXPECT_EQ(dict.lookup(text, len(text)), ustring(U"ホゲホゲ"));
}

TEST(test_text_helpers) {
    EXPECT_EQ(slice(U"あいう", 1, 10), ustring(U"いう"));
    EXPECT_EQ(slice(U"あいう", 2, 1), ustring(U""));
    EXPECT_EQ(translate(U"かな", kana::HIRAGANA, kana::KATAKANA), ustring(U"カナ"));
    EXPECT_EQ(kana::to_hankaku(U"ガッコウ"), ustring(U"ｶﾞｯｺｳ"));
    EXPECT_EQ(kana::to_zenkaku(U"A1 "), ustring(U"Ａ１　"));
    EXPECT_TRUE(is_numeric(U'3'));
    EXPECT_TRUE(is_numeric(U'３'));
    EXPECT_TRUE(is_numeric(U'五'));
    EXPECT_TRUE(!is_numeric(U'あ'));
    ustring emoji = U"\U0001F600あ";
    EXPECT_EQ(utf16_length(emoji), static_cast<size_t>(3));
    EXPECT_EQ(from_utf16(to_utf16(emoji)), emoji);
    EXPECT_EQ(from_utf8(to_utf8(emoji)), emoji);
}

TEST(test_json) {
    auto v = json::parse(R"({"a": [1, "xあ", true, null], "b": {"c": false}})");
    EXPECT_EQ(v["a"].as_array().size(), static_cast<size_t>(4));
    EXPECT_EQ(v["a"].as_array()[1].as_string(), std::string("x\xE3\x81\x82"));
    EXPECT_TRUE(v["a"].as_array()[2].as_bool());
    EXPECT_TRUE(!v["b"]["c"].as_bool(true));
    EXPECT_TRUE(v["missing"].is_null());
    auto again = json::parse(v.dump());
    EXPECT_EQ(again.dump(), v.dump());
}

TEST(test_settings_roundtrip) {
    TempDir dir;
    Settings s;
    s.layout = "jis";
    s.permissible = true;
    s.history_generation = 7;
    EXPECT_TRUE(s.save(dir.path / "settings.json"));
    Settings t = Settings::load(dir.path / "settings.json");
    EXPECT_EQ(t.layout, std::string("jis"));
    EXPECT_TRUE(t.permissible);
    EXPECT_EQ(t.history_generation, 7LL);
    Settings missing = Settings::load(dir.path / "none.json");
    EXPECT_EQ(missing.dictionary, std::string("restrained.9.dic"));
}
