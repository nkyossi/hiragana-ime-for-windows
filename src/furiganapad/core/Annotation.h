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

#pragma once

// Ruby annotations in plain text and the helpers of textbuffer.py.
//
// A ruby is written as IAA base IAS reading IAT, e.g. "￹漢字￺かんじ￻".

#include <string>
#include <string_view>

namespace furiganapad {

using ustring = std::u32string;
using ustring_view = std::u32string_view;
using index_t = std::ptrdiff_t;

inline constexpr char32_t IAA = 0xFFF9;  // INTERLINEAR ANNOTATION ANCHOR
inline constexpr char32_t IAS = 0xFFFA;  // INTERLINEAR ANNOTATION SEPARATOR
inline constexpr char32_t IAT = 0xFFFB;  // INTERLINEAR ANNOTATION TERMINATOR

// NEWLINES in textbuffer.py
inline constexpr ustring_view NEWLINES = U"\n\r\v\f\x1c\x1d\x1e\x85  ";
// HIRAGANA in textbuffer.py
inline constexpr ustring_view HIRAGANA =
    U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
    U"ゔがぎぐげござじずぜぞだぢづでどばびぶべぼぁぃぅぇぉゃゅょっぱぴぷぺぽゎゐゑ・ーゝゞ";

inline bool contains(ustring_view set, char32_t c) { return set.find(c) != ustring_view::npos; }

// KANZI = [一-鿿﨎-鶴侮-頻恵-舘𠮟]
bool is_kanzi(char32_t c);
// re.search(KANZI, s): the first run of kanzi, or false.
bool search_kanzi(const ustring& s, index_t& start, index_t& end);

bool is_reading(const ustring& s);
ustring get_plain_text(const ustring& s);
bool has_newline(const ustring& s);
ustring remove_dangling_annotations(const ustring& s);
// Python str.isspace() for one character.
bool is_space(char32_t c);
// Python str.splitlines(keepends=True) with the NEWLINES above.
std::u32string_view::size_type next_line_break(const ustring& s, size_t from, size_t& next);

std::string to_utf8(const ustring& s);
ustring from_utf8(std::string_view s);
std::wstring to_wide(const ustring& s);
ustring from_wide(std::wstring_view s);

}  // namespace furiganapad
