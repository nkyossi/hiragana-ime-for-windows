// FuriganaPad for Windows
//
// Copyright (c) 2019-2025 Esrille Inc. (FuriganaPad)
// Windows port: Copyright (c) 2026 Hiragana IME for Windows contributors
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, see <http://www.gnu.org/licenses/>.

#include "Breaker.h"

#include <windows.h>

#include <icu.h>

namespace furiganapad {

namespace {

enum Mode { PLAIN = 0, BASE = 1, RUBY = 2 };

// Note 'を' is intentionally removed from HIRAGANA_BREAK.
constexpr ustring_view HIRAGANA_BREAK =
    U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわん"
    U"ゔがぎぐげござじずぜぞだぢづでどばびぶべぼぁぃぅぇぉゃゅょっぱぴぷぺぽゎゐゑゝゞ";

bool is_hiragana_break(const ustring& s, index_t offset) {
    if (offset <= 0 || static_cast<index_t>(s.size()) <= offset) return false;
    char32_t a = s[static_cast<size_t>(offset - 1)];
    char32_t b = s[static_cast<size_t>(offset)];
    return contains(HIRAGANA_BREAK, a) && (contains(HIRAGANA_BREAK, b) || b == U'を');
}

// Code point offsets of the boundaries found by an ICU break iterator.
std::vector<index_t> boundaries(UBreakIteratorType type, const ustring& text) {
    std::wstring u16 = to_wide(text);
    // UTF-16 offset -> code point offset
    std::vector<index_t> cp(u16.size() + 1, 0);
    index_t n = 0;
    for (size_t i = 0; i < u16.size(); ++i) {
        cp[i] = n;
        if (!(0xD800 <= u16[i] && u16[i] <= 0xDBFF)) ++n;
    }
    cp[u16.size()] = n;

    std::vector<index_t> result;
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator* it = ubrk_open(type, "ja_JP", reinterpret_cast<const UChar*>(u16.c_str()),
                                   static_cast<int32_t>(u16.size()), &status);
    if (U_FAILURE(status) || !it) {
        // Fall back to every code point.
        for (index_t i = 0; i <= n; ++i) result.push_back(i);
        return result;
    }
    for (int32_t b = ubrk_first(it); b != UBRK_DONE; b = ubrk_next(it)) result.push_back(cp[static_cast<size_t>(b)]);
    ubrk_close(it);
    return result;
}

}  // namespace

index_t Breaker::following(index_t offset) const {
    for (;;) {
        ++offset;
        if (length_ < offset) return offset;
        if (cursor_positions_[static_cast<size_t>(offset)]) return offset;
    }
}

index_t Breaker::following_word_end(index_t offset) const {
    for (;;) {
        ++offset;
        if (length_ < offset) return offset;
        if (word_ends_[static_cast<size_t>(offset)]) return offset;
    }
}

index_t Breaker::preceding(index_t offset) const {
    for (;;) {
        --offset;
        if (offset < 0) return offset;
        if (cursor_positions_[static_cast<size_t>(offset)]) return offset;
    }
}

index_t Breaker::preceding_word_start(index_t offset) const {
    for (;;) {
        --offset;
        if (offset < 0) return offset;
        if (word_starts_[static_cast<size_t>(offset)]) return offset;
    }
}

void Breaker::set_text(const ustring& text) {
    length_ = static_cast<index_t>(text.size());
    if (text.empty()) {
        cursor_positions_.clear();
        word_starts_.clear();
        word_ends_.clear();
        return;
    }
    size_t n = text.size() + 1;
    cursor_positions_.assign(n, 0);
    word_starts_.assign(n, 0);
    word_ends_.assign(n, 0);
    auto at = [&](index_t i) -> char32_t { return i < length_ ? text[static_cast<size_t>(i)] : 0; };

    // Calculate cursor_positions
    std::vector<index_t> chars = boundaries(UBRK_CHARACTER, text);
    index_t offset = chars.empty() ? 0 : chars[0];
    cursor_positions_[static_cast<size_t>(offset)] = 1;
    Mode mode = PLAIN;
    for (size_t k = 1; k < chars.size(); ++k) {
        char32_t c = at(offset);  // previous character
        offset = chars[k];
        char32_t d = at(offset);  // next character
        if (c == IAA)
            mode = BASE;
        else if (d == IAS)
            mode = RUBY;
        else if (d == IAT)
            mode = PLAIN;
        else if (mode != RUBY)
            cursor_positions_[static_cast<size_t>(offset)] = 1;
    }
    cursor_positions_.back() = 1;

    // Calculate word_starts and word_ends
    std::vector<index_t> words = boundaries(UBRK_WORD, text);
    offset = words.empty() ? 0 : words[0];
    for (size_t k = 1; k < words.size(); ++k) {
        if (cursor_positions_[static_cast<size_t>(offset)]) {
            if (!is_space(at(offset)) && !is_hiragana_break(text, offset)) word_starts_[static_cast<size_t>(offset)] = 1;
        } else if (0 < offset && at(offset - 1) == IAA) {
            word_starts_[static_cast<size_t>(offset - 1)] = 1;
        }
        offset = words[k];
        if (cursor_positions_[static_cast<size_t>(offset)]) {
            if (0 < offset && !is_space(at(offset - 1)) && !is_hiragana_break(text, offset))
                word_ends_[static_cast<size_t>(offset)] = 1;
        } else if (1 < offset && at(offset - 1) == IAA) {
            word_ends_[static_cast<size_t>(offset - 1)] = 1;
        }
    }
    word_starts_[0] = 1;
    word_ends_.back() = 1;
}

}  // namespace furiganapad
