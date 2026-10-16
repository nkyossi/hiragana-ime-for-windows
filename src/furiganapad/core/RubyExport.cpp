// FuriganaPad for Windows
//
// Copyright (c) 2026 Hiragana IME for Windows contributors
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

#include "RubyExport.h"

#include <cstdio>
#include <functional>

namespace furiganapad {

namespace {

// Calls plain() for the text outside the annotations and ruby() for each
// annotation. A broken annotation is written as plain text without the marks.
void walk(const ustring& text, const std::function<void(const ustring&)>& plain,
          const std::function<void(const ustring&, const ustring&)>& ruby) {
    ustring run;
    size_t i = 0;
    while (i < text.size()) {
        char32_t c = text[i];
        if (c == IAA) {
            size_t sep = text.find(IAS, i + 1);
            size_t end = sep == ustring::npos ? ustring::npos : text.find(IAT, sep + 1);
            size_t next = text.find(IAA, i + 1);
            if (end != ustring::npos && (next == ustring::npos || end < next)) {
                if (!run.empty()) plain(run);
                run.clear();
                ruby(text.substr(i + 1, sep - i - 1), text.substr(sep + 1, end - sep - 1));
                i = end + 1;
                continue;
            }
        } else if (c != IAS && c != IAT) {
            run += c;
        }
        ++i;
    }
    if (!run.empty()) plain(run);
}

void escape_html(const ustring& s, ustring& out) {
    for (char32_t c : s) {
        switch (c) {
            case U'&': out += U"&amp;"; break;
            case U'<': out += U"&lt;"; break;
            case U'>': out += U"&gt;"; break;
            case U'"': out += U"&quot;"; break;
            case U'\r': break;
            case U'\n': out += U"<br>\n"; break;
            default: out += c; break;
        }
    }
}

}  // namespace

ustring to_note_ruby(const ustring& text) {
    ustring out;
    walk(
        text, [&](const ustring& s) { out += s; },
        [&](const ustring& base, const ustring& reading) { out += U"|" + base + U"《" + reading + U"》"; });
    return out;
}

ustring to_html_ruby(const ustring& text) {
    ustring out;
    walk(
        text, [&](const ustring& s) { escape_html(s, out); },
        [&](const ustring& base, const ustring& reading) {
            out += U"<ruby>";
            escape_html(base, out);
            out += U"<rp>(</rp><rt>";
            escape_html(reading, out);
            out += U"</rt><rp>)</rp></ruby>";
        });
    return out;
}

std::string make_cf_html(const std::string& fragment) {
    // The offsets are byte offsets into the UTF-8 data; the header has a
    // fixed size because the numbers are written with 10 digits.
    const std::string header_format =
        "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\nStartFragment:%010zu\r\nEndFragment:%010zu\r\n";
    const std::string before = "<html><head><meta charset=\"utf-8\"></head><body>\r\n<!--StartFragment-->";
    const std::string after = "<!--EndFragment-->\r\n</body></html>";
    char header[160];
    size_t header_size = static_cast<size_t>(std::snprintf(header, sizeof header, header_format.c_str(),
                                                           size_t{0}, size_t{0}, size_t{0}, size_t{0}));
    size_t start_html = header_size;
    size_t start_fragment = start_html + before.size();
    size_t end_fragment = start_fragment + fragment.size();
    size_t end_html = end_fragment + after.size();
    std::snprintf(header, sizeof header, header_format.c_str(), start_html, end_html, start_fragment, end_fragment);
    return std::string(header) + before + fragment + after;
}

}  // namespace furiganapad
