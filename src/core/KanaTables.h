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

#pragma once

// Character tables from engine/dictionary.py and engine/engine.py.

#include "Text.h"

namespace hiragana::kana {

// dictionary.py
inline constexpr ustring_view HIRAGANA =
    U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわゐゑをん"
    U"ゔがぎぐげござじずぜぞだぢづでどばびぶべぼぁぃぅぇぉゃゅょっゎぱぴぷぺぽ・ーゝゞ";
inline constexpr ustring_view KATAKANA =
    U"アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヰヱヲン"
    U"ヴガギグゲゴザジズゼゾダヂヅデドバビブベボァィゥェォャュョッヮパピプペポ・ーヽヾ";
inline constexpr ustring_view DAKUON = U"がぎぐげござじずぜぞだぢづでどばびぶべぼ";
inline constexpr ustring_view SEION = U"かきくけこさしすせそたちつてとはひふへほ";
inline constexpr ustring_view SUFFIX_CODES = U"1iIkKgsStnbmrwW235";

// engine.py
inline constexpr ustring_view NON_DAKU =
    U"あいうえおかきくけこさしすせそたちつてとはひふへほやゆよわ"
    U"アイウエオカキクケコサシスセソタチツテトハヒフヘホヤユヨワ"
    U"ぁぃぅぇぉがぎぐげござじずぜぞだぢづでどばびぶべぼゃゅょゎ"
    U"ァィゥェォガギグゲゴザジズゼゾダヂヅデドバビブベボャュョヮ"
    U"ゔヴゝヽゞヾ。「」、・｡｢｣､･";
inline constexpr ustring_view DAKU =
    U"ぁぃぅぇぉがぎぐげござじずぜぞだぢづでどばびぶべぼゃゅょゎ"
    U"ァィゥェォガギグゲゴザジズゼゾダヂヅデドバビブベボャュョヮ"
    U"あいゔえおかきくけこさしすせそたちつてとはひふへほやゆよわ"
    U"アイヴエオカキクケコサシスセソタチツテトハヒフヘホヤユヨワ"
    U"うウゞヾゝヽ｡｢｣､･。「」、・";
inline constexpr ustring_view NON_HANDAKU = U"はひふへほハヒフヘホぱぴぷぺぽパピプペポ";
inline constexpr ustring_view HANDAKU = U"ぱぴぷぺぽパピプペポはひふへほハヒフヘホ";
inline constexpr ustring_view NON_TINY =
    U"あいうえおつやゆよわアイウエオツヤユヨワぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮ。「」、・｡｢｣､･";
inline constexpr ustring_view TINY =
    U"ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮあいうえおつやゆよわアイウエオツヤユヨワ｡｢｣､･。「」、・";
inline constexpr ustring_view OKURIGANA =
    U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわゐゑをん"
    U"ゔがぎぐげござじずぜぞだぢづでどばびぶべぼぁぃぅぇぉゃゅょっゎぱぴぷぺぽ゛゜";
inline constexpr ustring_view HANKAKU_DIGIT = U"0123456789";
inline constexpr ustring_view CIRCUMFLEX = U"âîûêôÂÎÛÊÔ";
inline constexpr ustring_view MACRON = U"āīūēōĀĪŪĒŌ";
inline constexpr ustring_view VOWELS = U"aiueoAIUEO";
inline constexpr ustring_view SOKUON = U"ksthmyrwgzdbpfjv";

inline constexpr char32_t DASH = U'―';     // marks the end of a yôgen stem
inline constexpr char32_t DAKUTEN = U'゛';
inline constexpr char32_t HANDAKUTEN = U'゜';

inline bool is_hiragana(char32_t c) { return contains(HIRAGANA, c); }
inline bool is_katakana(char32_t c) { return contains(KATAKANA, c); }

ustring to_katakana(const ustring& s);
ustring to_hiragana(const ustring& s);
ustring to_hankaku(const ustring& kana);
ustring to_zenkaku(const ustring& asc);
ustring to_hankaku_digit(const ustring& s);
ustring to_hankaku_digit_extra(const ustring& s);
ustring from_circumflex(const ustring& s);
ustring to_circumflex(const ustring& s);
ustring from_macron(const ustring& s);
ustring to_macron(const ustring& s);

// Length of the match of ^[HIRAGANA]+ (RE_PREFIX), 0 if none.
size_t hiragana_prefix_length(const ustring& s);

}  // namespace hiragana::kana
