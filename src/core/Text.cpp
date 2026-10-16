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

#include "Text.h"

#include <algorithm>
#include <iterator>

namespace hiragana {

namespace {

struct Range {
    char32_t first;
    char32_t last;
};

const Range kNumeric[] = {
#include "NumericTable.inc"
};

}  // namespace

ustring replace_all(const ustring& s, const ustring& from, const ustring& to) {
    if (from.empty()) return s;
    ustring result;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(from, start);
        if (pos == ustring::npos) break;
        result.append(s, start, pos - start);
        result += to;
        start = pos + from.size();
    }
    result.append(s, start, ustring::npos);
    return result;
}

ustring translate(const ustring& s, ustring_view from, ustring_view to) {
    ustring result;
    result.reserve(s.size());
    for (char32_t c : s) {
        // str.maketrans() keeps the last mapping for a duplicated key.
        size_t pos = from.rfind(c);
        result += (pos == ustring_view::npos || pos >= to.size()) ? c : to[pos];
    }
    return result;
}

bool is_numeric(char32_t c) {
    auto it = std::upper_bound(std::begin(kNumeric), std::end(kNumeric), c,
                               [](char32_t value, const Range& r) { return value < r.first; });
    if (it == std::begin(kNumeric)) return false;
    --it;
    return it->first <= c && c <= it->last;
}

char32_t to_lower(char32_t c) {
    if (U'A' <= c && c <= U'Z') return c + 0x20;
    // Latin-1 Supplement uppercase letters (except ×) and the macron letters in Latin Extended-A.
    if (0xC0 <= c && c <= 0xDE && c != 0xD7) return c + 0x20;
    if (0x100 <= c && c <= 0x17F && (c % 2) == 0 && c != 0x130 && c != 0x138) return c + 1;
    if (0xFF21 <= c && c <= 0xFF3A) return c + 0x20;  // Fullwidth A-Z
    return c;
}

ustring from_utf8(std::string_view s) {
    ustring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        size_t extra;
        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            out += U'�';
            ++i;
            continue;
        }
        bool ok = true;
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= s.size()) {
                ok = false;
                break;
            }
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) {
            out += U'�';
            ++i;
            continue;
        }
        out += cp;
        i += extra + 1;
    }
    return out;
}

std::string to_utf8(const ustring& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (char32_t cp : s) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

ustring from_utf16(std::u16string_view s) {
    ustring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char16_t c = s[i];
        if (0xD800 <= c && c <= 0xDBFF && i + 1 < s.size() && 0xDC00 <= s[i + 1] && s[i + 1] <= 0xDFFF) {
            out += static_cast<char32_t>(0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00));
            ++i;
        } else if (0xD800 <= c && c <= 0xDFFF) {
            out += U'�';
        } else {
            out += c;
        }
    }
    return out;
}

std::u16string to_utf16(const ustring& s) {
    std::u16string out;
    out.reserve(s.size());
    for (char32_t cp : s) {
        if (cp < 0x10000) {
            out += static_cast<char16_t>(cp);
        } else {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (cp >> 10));
            out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        }
    }
    return out;
}

ustring from_wide(std::wstring_view s) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t), "Windows wchar_t is UTF-16");
    return from_utf16(std::u16string_view(reinterpret_cast<const char16_t*>(s.data()), s.size()));
}

std::wstring to_wide(const ustring& s) {
    std::u16string u = to_utf16(s);
    return std::wstring(u.begin(), u.end());
}

size_t utf16_length(const ustring& s) {
    size_t n = 0;
    for (char32_t cp : s) n += cp < 0x10000 ? 1 : 2;
    return n;
}

}  // namespace hiragana
