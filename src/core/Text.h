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

// String helpers that mimic the Python str operations used by ibus-hiragana.
// The core works on std::u32string so that lengths and indices count code
// points exactly like Python does.

#include <cstddef>
#include <string>
#include <string_view>

namespace hiragana {

using ustring = std::u32string;
using ustring_view = std::u32string_view;
// Python int for string indices and lengths.
using index_t = std::ptrdiff_t;

// Python-style slice s[a:b] with clamping (negative indices are not used by the port).
inline ustring slice(const ustring& s, index_t a, index_t b) {
    index_t n = static_cast<index_t>(s.size());
    if (a < 0) a = 0;
    if (b > n) b = n;
    if (a >= b) return ustring();
    return s.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
}

inline ustring slice_from(const ustring& s, index_t a) {
    return slice(s, a, static_cast<index_t>(s.size()));
}

inline ustring slice_to(const ustring& s, index_t b) {
    return slice(s, 0, b);
}

inline index_t len(const ustring& s) {
    return static_cast<index_t>(s.size());
}

inline bool contains(ustring_view set, char32_t c) {
    return set.find(c) != ustring_view::npos;
}

// Python str.find(c) for a single character.
inline index_t find(ustring_view s, char32_t c) {
    size_t pos = s.find(c);
    return pos == ustring_view::npos ? -1 : static_cast<index_t>(pos);
}

// Python str.rfind(sub).
inline index_t rfind(const ustring& s, const ustring& sub) {
    size_t pos = s.rfind(sub);
    return pos == ustring::npos ? -1 : static_cast<index_t>(pos);
}

inline bool starts_with(const ustring& s, const ustring& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool ends_with(const ustring& s, const ustring& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline char32_t last(const ustring& s) {
    return s.empty() ? 0 : s.back();
}

// Python str.replace(old, new) replacing all occurrences.
ustring replace_all(const ustring& s, const ustring& from, const ustring& to);

// Python str.translate(str.maketrans(from, to)) where from and to have the same length.
// A later duplicate in `from` wins, as with str.maketrans().
ustring translate(const ustring& s, ustring_view from, ustring_view to);

// Python str.isnumeric() for a single character.
bool is_numeric(char32_t c);

// Python str.lower() for a single character (enough for the characters the engine uses).
char32_t to_lower(char32_t c);

// Encoding conversions. Invalid sequences are replaced with U+FFFD.
ustring from_utf8(std::string_view s);
std::string to_utf8(const ustring& s);
ustring from_utf16(std::u16string_view s);
std::u16string to_utf16(const ustring& s);
ustring from_wide(std::wstring_view s);
std::wstring to_wide(const ustring& s);

// Number of UTF-16 code units needed for s.
size_t utf16_length(const ustring& s);

}  // namespace hiragana
