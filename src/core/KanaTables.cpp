// Hiragana IME for Windows
//
// Copyright (c) 2017-2026 Esrille Inc. (ibus-hiragana)
// Windows port: Copyright (c) 2026 Hiragana IME for Windows contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "KanaTables.h"

#include <unordered_map>

namespace hiragana::kana {

namespace {

ustring make_range(char32_t first, char32_t last) {
    ustring s;
    for (char32_t c = first; c <= last; ++c) s += c;
    return s;
}

// ZENKAKU / HANKAKU in engine.py
const ustring& zenkaku() {
    static const ustring s = make_range(0xff01, 0xff5e) + U"　〔〕［］￥？";
    return s;
}

const ustring& hankaku() {
    static const ustring s = make_range(0x21, 0x7e) + U" ❲❳[]¥?";
    return s;
}

const std::unordered_map<char32_t, ustring>& hankaku_kana() {
    static const std::unordered_map<char32_t, ustring> m = {
        {U'。', U"｡"}, {U'「', U"｢"}, {U'」', U"｣"}, {U'、', U"､"}, {U'・', U"･"},
        {U'ヲ', U"ｦ"},
        {U'ァ', U"ｧ"}, {U'ィ', U"ｨ"}, {U'ゥ', U"ｩ"}, {U'ェ', U"ｪ"}, {U'ォ', U"ｫ"},
        {U'ャ', U"ｬ"}, {U'ュ', U"ｭ"}, {U'ョ', U"ｮ"},
        {U'ッ', U"ｯ"}, {U'ー', U"ｰ"},
        {U'ア', U"ｱ"}, {U'イ', U"ｲ"}, {U'ウ', U"ｳ"}, {U'エ', U"ｴ"}, {U'オ', U"ｵ"},
        {U'カ', U"ｶ"}, {U'キ', U"ｷ"}, {U'ク', U"ｸ"}, {U'ケ', U"ｹ"}, {U'コ', U"ｺ"},
        {U'サ', U"ｻ"}, {U'シ', U"ｼ"}, {U'ス', U"ｽ"}, {U'セ', U"ｾ"}, {U'ソ', U"ｿ"},
        {U'タ', U"ﾀ"}, {U'チ', U"ﾁ"}, {U'ツ', U"ﾂ"}, {U'テ', U"ﾃ"}, {U'ト', U"ﾄ"},
        {U'ナ', U"ﾅ"}, {U'ニ', U"ﾆ"}, {U'ヌ', U"ﾇ"}, {U'ネ', U"ﾈ"}, {U'ノ', U"ﾉ"},
        {U'ハ', U"ﾊ"}, {U'ヒ', U"ﾋ"}, {U'フ', U"ﾌ"}, {U'ヘ', U"ﾍ"}, {U'ホ', U"ﾎ"},
        {U'マ', U"ﾏ"}, {U'ミ', U"ﾐ"}, {U'ム', U"ﾑ"}, {U'メ', U"ﾒ"}, {U'モ', U"ﾓ"},
        {U'ヤ', U"ﾔ"}, {U'ユ', U"ﾕ"}, {U'ヨ', U"ﾖ"},
        {U'ラ', U"ﾗ"}, {U'リ', U"ﾘ"}, {U'ル', U"ﾙ"}, {U'レ', U"ﾚ"}, {U'ロ', U"ﾛ"},
        {U'ワ', U"ﾜ"}, {U'ン', U"ﾝ"}, {U'゛', U"ﾞ"}, {U'゜', U"ﾟ"},
        {U'ガ', U"ｶﾞ"}, {U'ギ', U"ｷﾞ"}, {U'グ', U"ｸﾞ"}, {U'ゲ', U"ｹﾞ"}, {U'ゴ', U"ｺﾞ"},
        {U'ザ', U"ｻﾞ"}, {U'ジ', U"ｼﾞ"}, {U'ズ', U"ｽﾞ"}, {U'ゼ', U"ｾﾞ"}, {U'ゾ', U"ｿﾞ"},
        {U'ダ', U"ﾀﾞ"}, {U'ヂ', U"ﾁﾞ"}, {U'ヅ', U"ﾂﾞ"}, {U'デ', U"ﾃﾞ"}, {U'ド', U"ﾄﾞ"},
        {U'バ', U"ﾊﾞ"}, {U'ビ', U"ﾋﾞ"}, {U'ブ', U"ﾌﾞ"}, {U'ベ', U"ﾍﾞ"}, {U'ボ', U"ﾎﾞ"},
        {U'パ', U"ﾊﾟ"}, {U'ピ', U"ﾋﾟ"}, {U'プ', U"ﾌﾟ"}, {U'ペ', U"ﾍﾟ"}, {U'ポ', U"ﾎﾟ"},
        {U'ヴ', U"ｳﾞ"},
    };
    return m;
}

}  // namespace

ustring to_katakana(const ustring& s) { return translate(s, HIRAGANA, KATAKANA); }
ustring to_hiragana(const ustring& s) { return translate(s, KATAKANA, HIRAGANA); }

ustring to_hankaku(const ustring& kana) {
    const auto& table = hankaku_kana();
    ustring result;
    for (char32_t c : kana) {
        c = translate(ustring(1, c), zenkaku(), hankaku())[0];
        auto it = table.find(c);
        if (it != table.end())
            result += it->second;
        else
            result += c;
    }
    return result;
}

ustring to_zenkaku(const ustring& asc) { return translate(asc, hankaku(), zenkaku()); }
ustring to_hankaku_digit(const ustring& s) { return translate(s, U"０１２３４５６７８９", U"0123456789"); }
ustring to_hankaku_digit_extra(const ustring& s) {
    return translate(s, U"０１２３４５６７８９、。％", U"0123456789,.%");
}
ustring from_circumflex(const ustring& s) { return translate(s, CIRCUMFLEX, VOWELS); }
ustring to_circumflex(const ustring& s) { return translate(s, VOWELS, CIRCUMFLEX); }
ustring from_macron(const ustring& s) { return translate(s, MACRON, VOWELS); }
ustring to_macron(const ustring& s) { return translate(s, VOWELS, MACRON); }

size_t hiragana_prefix_length(const ustring& s) {
    size_t n = 0;
    while (n < s.size() && is_hiragana(s[n])) ++n;
    return n;
}

}  // namespace hiragana::kana
