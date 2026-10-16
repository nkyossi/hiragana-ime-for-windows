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

#include "Annotation.h"

namespace furiganapad {

namespace {

enum Mode { PLAIN = 0, BASE = 1, RUBY = 2 };

}  // namespace

bool is_kanzi(char32_t c) {
    return (0x4E00 <= c && c <= 0x9FFF) || (0xFA0E <= c && c <= 0xFA2D) || (0xFA30 <= c && c <= 0xFA6A) ||
           (0xFA6B <= c && c <= 0xFA6D) || c == 0x20B9F;  // 𠮟
}

bool search_kanzi(const ustring& s, index_t& start, index_t& end) {
    size_t i = 0;
    while (i < s.size() && !is_kanzi(s[i])) ++i;
    if (i == s.size()) return false;
    size_t j = i;
    while (j < s.size() && is_kanzi(s[j])) ++j;
    start = static_cast<index_t>(i);
    end = static_cast<index_t>(j);
    return true;
}

bool is_reading(const ustring& s) {
    if (s.empty()) return false;
    if (!contains(HIRAGANA, s[0])) return false;
    for (size_t i = 1; i < s.size(); ++i) {
        char32_t c = s[i];
        if (c == U'―') return true;
        if (!contains(HIRAGANA, c)) return false;
    }
    return true;
}

ustring get_plain_text(const ustring& s) {
    Mode mode = PLAIN;
    ustring plain;
    for (char32_t c : s) {
        if (c == IAA)
            mode = BASE;
        else if (c == IAS)
            mode = RUBY;
        else if (c == IAT)
            mode = PLAIN;
        else if (mode != RUBY)
            plain += c;
    }
    return plain;
}

bool has_newline(const ustring& s) {
    for (char32_t c : s) {
        if (contains(NEWLINES, c)) return true;
    }
    return false;
}

ustring remove_dangling_annotations(const ustring& s_in) {
    // A literal port, including the handling of the index in the first loop.
    ustring t;
    size_t i = 0;
    Mode mode = PLAIN;
    for (char32_t c : s_in) {
        if (c == IAA) {
            if (mode != PLAIN) continue;
            break;
        } else if (c == IAS) {
            mode = RUBY;
        } else if (c == IAT) {
            if (mode != RUBY) t.clear();
            i = i + 1;
            break;
        } else if (mode == PLAIN) {
            t += c;
        }
        i += 1;
    }
    mode = PLAIN;
    ustring s = i < s_in.size() ? s_in.substr(i) : ustring();
    ustring a;
    for (char32_t c : s) {
        if (c == IAA) {
            if (mode != PLAIN) continue;
            mode = BASE;
            a += c;
        } else if (c == IAS) {
            if (mode != BASE) continue;
            mode = RUBY;
            a += c;
        } else if (c == IAT) {
            if (mode != RUBY) continue;
            mode = PLAIN;
            a += c;
            t += a;
            a.clear();
        } else if (mode == PLAIN) {
            t += c;
        } else {
            a += c;
        }
    }
    if (mode != PLAIN) t += remove_dangling_annotations(a.size() > 1 ? a.substr(1) : ustring());
    return t;
}

bool is_space(char32_t c) {
    switch (c) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20:
        case 0x85: case 0xA0: case 0x1680:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return 0x2000 <= c && c <= 0x200A;
    }
}

size_t next_line_break(const ustring& s, size_t from, size_t& next) {
    // Returns the position of the next line break at or after from and sets
    // next to the start of the following line ("\r\n" is one line break).
    for (size_t i = from; i < s.size(); ++i) {
        if (contains(NEWLINES, s[i])) {
            next = (s[i] == U'\r' && i + 1 < s.size() && s[i + 1] == U'\n') ? i + 2 : i + 1;
            return i;
        }
    }
    next = s.size();
    return ustring::npos;
}

std::string to_utf8(const ustring& s) {
    std::string out;
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

ustring from_utf8(std::string_view s) {
    // Invalid bytes are dropped, as bytes.decode('utf-8', 'ignore') does.
    ustring out;
    size_t i = 0;
    if (s.substr(0, 3) == "\xEF\xBB\xBF") i = 3;
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
            ++i;
            continue;
        }
        bool ok = true;
        for (size_t k = 1; ok && k <= extra; ++k) {
            if (i + k >= s.size() || (static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        if (!ok) {
            ++i;
            continue;
        }
        out += cp;
        i += extra + 1;
    }
    return out;
}

std::wstring to_wide(const ustring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (char32_t cp : s) {
        if (cp < 0x10000) {
            out += static_cast<wchar_t>(cp);
        } else {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        }
    }
    return out;
}

ustring from_wide(std::wstring_view s) {
    ustring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (0xD800 <= c && c <= 0xDBFF && i + 1 < s.size() && 0xDC00 <= s[i + 1] && s[i + 1] <= 0xDFFF) {
            out += static_cast<char32_t>(0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00));
            ++i;
        } else if (0xD800 <= c && c <= 0xDFFF) {
            out += U'�';
        } else {
            out += static_cast<char32_t>(c);
        }
    }
    return out;
}

}  // namespace furiganapad
