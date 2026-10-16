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

#pragma once

// Markdown support, new in the Windows version:
// - Rubies are saved as HTML ruby tags, the format of tools/convert_to_tag.py:
//   <ruby>漢字<rp>(</rp><rt>かんじ</rt><rp>)</rp></ruby>
// - The Markdown syntax is shown with styles while editing; the source text
//   itself is not changed.

#include <vector>

#include "Annotation.h"

namespace furiganapad {

// IAA/IAS/IAT -> <ruby> tags
ustring annotated_to_markdown(const ustring& text);
// <ruby> tags (with or without <rp> and <rb>) -> IAA/IAS/IAT. Other text is kept as is.
ustring markdown_to_annotated(const ustring& text);

struct MarkdownSpan {
    enum Kind {
        Marker,  // syntax characters such as '#', '**', '`'
        Bold,
        Italic,
        Code,
        Strike,
        Link,
    };
    index_t start;  // offset in the plain text of the paragraph
    index_t end;
    Kind kind;
};

struct MarkdownParagraph {
    int heading = 0;  // 1..6 for a heading
    bool code_block = false;
    bool quote = false;
    bool rule = false;  // thematic break
    std::vector<MarkdownSpan> spans;
};

// Styles of each paragraph; plain is the text of the paragraphs without rubies.
std::vector<MarkdownParagraph> markdown_styles(const std::vector<ustring>& plain);

}  // namespace furiganapad
