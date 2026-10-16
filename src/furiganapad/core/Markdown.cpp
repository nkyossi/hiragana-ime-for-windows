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

#include "Markdown.h"

#include <cwctype>

namespace furiganapad {

namespace {

const ustring kRubyOpen = U"<ruby>";
const ustring kRubyClose = U"</ruby>";
const ustring kRpOpen = U"<rp>";
const ustring kRpClose = U"</rp>";
const ustring kRtOpen = U"<rt>";
const ustring kRtClose = U"</rt>";
const ustring kRbOpen = U"<rb>";
const ustring kRbClose = U"</rb>";

bool at(const ustring& s, size_t i, const ustring& token) { return s.compare(i, token.size(), token) == 0; }

ustring remove_all(const ustring& s, const ustring& token) {
    ustring out;
    size_t i = 0;
    for (;;) {
        size_t p = s.find(token, i);
        if (p == ustring::npos) {
            out += s.substr(i);
            return out;
        }
        out += s.substr(i, p - i);
        i = p + token.size();
    }
}

// Converts the inside of <ruby>...</ruby>; returns false if it is not understood.
bool convert_ruby(const ustring& inner_in, ustring& out) {
    // Drop <rp>...</rp> and the <rb> tags.
    ustring inner;
    for (size_t i = 0; i < inner_in.size();) {
        if (at(inner_in, i, kRpOpen)) {
            size_t close = inner_in.find(kRpClose, i);
            if (close == ustring::npos) return false;
            i = close + kRpClose.size();
        } else {
            inner += inner_in[i++];
        }
    }
    inner = remove_all(remove_all(inner, kRbOpen), kRbClose);

    ustring result;
    size_t i = 0;
    bool any = false;
    while (i < inner.size()) {
        size_t rt = inner.find(kRtOpen, i);
        if (rt == ustring::npos) {
            result += inner.substr(i);  // base text without a reading
            break;
        }
        size_t close = inner.find(kRtClose, rt);
        if (close == ustring::npos) return false;
        ustring base = inner.substr(i, rt - i);
        ustring reading = inner.substr(rt + kRtOpen.size(), close - rt - kRtOpen.size());
        if (base.empty() || base.find(U'<') != ustring::npos || reading.find(U'<') != ustring::npos) return false;
        if (reading.empty()) {
            result += base;
        } else {
            result += IAA;
            result += base;
            result += IAS;
            result += reading;
            result += IAT;
        }
        any = true;
        i = close + kRtClose.size();
    }
    if (!any) return false;
    out += result;
    return true;
}

bool is_digit(char32_t c) { return U'0' <= c && c <= U'9'; }

void add(std::vector<MarkdownSpan>& spans, index_t start, index_t end, MarkdownSpan::Kind kind) {
    if (start < end) spans.push_back({start, end, kind});
}

// Inline syntax: `code`, **bold**, __bold__, *italic*, _italic_, ~~strike~~, [text](url)
void parse_inline(const ustring& s, size_t from, std::vector<MarkdownSpan>& spans) {
    size_t i = from;
    const size_t n = s.size();
    auto idx = [](size_t v) { return static_cast<index_t>(v); };
    while (i < n) {
        char32_t c = s[i];
        if (c == U'\\' && i + 1 < n) {
            add(spans, idx(i), idx(i + 1), MarkdownSpan::Marker);
            i += 2;
            continue;
        }
        if (c == U'`') {
            size_t ticks = 1;
            while (i + ticks < n && s[i + ticks] == U'`') ++ticks;
            ustring fence(ticks, U'`');
            size_t close = s.find(fence, i + ticks);
            if (close != ustring::npos) {
                add(spans, idx(i), idx(i + ticks), MarkdownSpan::Marker);
                add(spans, idx(i + ticks), idx(close), MarkdownSpan::Code);
                add(spans, idx(close), idx(close + ticks), MarkdownSpan::Marker);
                i = close + ticks;
                continue;
            }
            i += ticks;
            continue;
        }
        if ((c == U'*' || c == U'_') && i + 1 < n && s[i + 1] == c) {
            ustring marker(2, c);
            size_t close = s.find(marker, i + 2);
            if (close != ustring::npos && close > i + 2) {
                add(spans, idx(i), idx(i + 2), MarkdownSpan::Marker);
                add(spans, idx(i + 2), idx(close), MarkdownSpan::Bold);
                parse_inline(s.substr(0, close), i + 2, spans);
                add(spans, idx(close), idx(close + 2), MarkdownSpan::Marker);
                i = close + 2;
                continue;
            }
        }
        if (c == U'~' && i + 1 < n && s[i + 1] == U'~') {
            size_t close = s.find(U"~~", i + 2);
            if (close != ustring::npos && close > i + 2) {
                add(spans, idx(i), idx(i + 2), MarkdownSpan::Marker);
                add(spans, idx(i + 2), idx(close), MarkdownSpan::Strike);
                add(spans, idx(close), idx(close + 2), MarkdownSpan::Marker);
                i = close + 2;
                continue;
            }
        }
        if ((c == U'*' || c == U'_') && i + 1 < n && !is_space(s[i + 1])) {
            // _ inside a word is not emphasis.
            bool word_before = c == U'_' && 0 < i && !is_space(s[i - 1]) && s[i - 1] < 0x80 &&
                               (std::iswalnum(static_cast<wint_t>(s[i - 1])) != 0);
            size_t close = s.find(c, i + 1);
            if (!word_before && close != ustring::npos && !is_space(s[close - 1])) {
                add(spans, idx(i), idx(i + 1), MarkdownSpan::Marker);
                add(spans, idx(i + 1), idx(close), MarkdownSpan::Italic);
                add(spans, idx(close), idx(close + 1), MarkdownSpan::Marker);
                i = close + 1;
                continue;
            }
        }
        if (c == U'[') {
            size_t close = s.find(U"](", i + 1);
            size_t end = close == ustring::npos ? ustring::npos : s.find(U')', close + 2);
            if (close != ustring::npos && end != ustring::npos && s.find(U'\n', i) > end) {
                add(spans, idx(i), idx(i + 1), MarkdownSpan::Marker);
                add(spans, idx(i + 1), idx(close), MarkdownSpan::Link);
                add(spans, idx(close), idx(end + 1), MarkdownSpan::Marker);
                i = end + 1;
                continue;
            }
        }
        ++i;
    }
}

}  // namespace

ustring annotated_to_markdown(const ustring& text) {
    ustring out;
    for (char32_t c : text) {
        if (c == IAA)
            out += kRubyOpen;
        else if (c == IAS)
            out += U"<rp>(</rp><rt>";
        else if (c == IAT)
            out += U"</rt><rp>)</rp></ruby>";
        else
            out += c;
    }
    return out;
}

ustring markdown_to_annotated(const ustring& text) {
    ustring out;
    size_t i = 0;
    while (i < text.size()) {
        size_t open = text.find(kRubyOpen, i);
        if (open == ustring::npos) {
            out += text.substr(i);
            break;
        }
        out += text.substr(i, open - i);
        size_t close = text.find(kRubyClose, open);
        size_t newline = text.find(U'\n', open);
        if (close == ustring::npos || (newline != ustring::npos && newline < close)) {
            out += kRubyOpen;
            i = open + kRubyOpen.size();
            continue;
        }
        ustring inner = text.substr(open + kRubyOpen.size(), close - open - kRubyOpen.size());
        if (!convert_ruby(inner, out)) out += text.substr(open, close + kRubyClose.size() - open);
        i = close + kRubyClose.size();
    }
    return out;
}

std::vector<MarkdownParagraph> markdown_styles(const std::vector<ustring>& plain) {
    std::vector<MarkdownParagraph> result(plain.size());
    bool in_fence = false;
    ustring fence;
    for (size_t k = 0; k < plain.size(); ++k) {
        const ustring& s = plain[k];
        MarkdownParagraph& p = result[k];
        auto idx = [](size_t v) { return static_cast<index_t>(v); };

        // Leading spaces (up to 3) are allowed before the block syntax.
        size_t lead = 0;
        while (lead < s.size() && lead < 3 && s[lead] == U' ') ++lead;
        ustring rest = s.substr(lead);

        bool is_fence = rest.size() >= 3 && (rest.compare(0, 3, U"```") == 0 || rest.compare(0, 3, U"~~~") == 0);
        if (in_fence) {
            p.code_block = true;
            if (is_fence && rest.compare(0, fence.size(), fence) == 0) {
                in_fence = false;
                add(p.spans, 0, idx(s.size()), MarkdownSpan::Marker);
            } else {
                add(p.spans, 0, idx(s.size()), MarkdownSpan::Code);
            }
            continue;
        }
        if (is_fence) {
            in_fence = true;
            fence = rest.substr(0, 3);
            p.code_block = true;
            add(p.spans, 0, idx(s.size()), MarkdownSpan::Marker);
            continue;
        }

        // Thematic break: three or more of the same *, - or _ with optional spaces.
        if (!rest.empty() && (rest[0] == U'*' || rest[0] == U'-' || rest[0] == U'_')) {
            size_t count = 0;
            bool only = true;
            for (char32_t c : rest) {
                if (c == rest[0])
                    ++count;
                else if (c != U' ' && c != U'\t')
                    only = false;
            }
            if (only && count >= 3) {
                p.rule = true;
                add(p.spans, 0, idx(s.size()), MarkdownSpan::Marker);
                continue;
            }
        }

        size_t body = lead;
        // ATX heading
        size_t hashes = 0;
        while (hashes < rest.size() && rest[hashes] == U'#') ++hashes;
        if (1 <= hashes && hashes <= 6 && (hashes == rest.size() || rest[hashes] == U' ' || rest[hashes] == U'\t')) {
            p.heading = static_cast<int>(hashes);
            body = lead + hashes;
            while (body < s.size() && (s[body] == U' ' || s[body] == U'\t')) ++body;
            add(p.spans, 0, idx(body), MarkdownSpan::Marker);
        } else {
            // Block quotes, possibly nested
            size_t q = lead;
            while (q < s.size() && s[q] == U'>') {
                p.quote = true;
                ++q;
                if (q < s.size() && s[q] == U' ') ++q;
            }
            if (p.quote) {
                add(p.spans, idx(lead), idx(q), MarkdownSpan::Marker);
                body = q;
            }
            // List items
            size_t l = body;
            while (l < s.size() && (s[l] == U' ' || s[l] == U'\t')) ++l;
            if (l + 1 < s.size() && (s[l] == U'-' || s[l] == U'*' || s[l] == U'+') && s[l + 1] == U' ') {
                add(p.spans, idx(l), idx(l + 1), MarkdownSpan::Marker);
                body = l + 2;
                // Task list item
                if (body + 2 < s.size() && s[body] == U'[' && s[body + 2] == U']') {
                    add(p.spans, idx(body), idx(body + 3), MarkdownSpan::Marker);
                    body += 3;
                }
            } else {
                size_t d = l;
                while (d < s.size() && is_digit(s[d])) ++d;
                if (l < d && d + 1 < s.size() && (s[d] == U'.' || s[d] == U')') && s[d + 1] == U' ') {
                    add(p.spans, idx(l), idx(d + 1), MarkdownSpan::Marker);
                    body = d + 2;
                }
            }
        }
        parse_inline(s, body, p.spans);
    }
    return result;
}

}  // namespace furiganapad
