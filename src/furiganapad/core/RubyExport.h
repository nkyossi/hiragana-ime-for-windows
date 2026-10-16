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

// Writes the ruby annotations (IAA base IAS reading IAT) in other notations
// for "Copy with Ruby".

#include "Annotation.h"

namespace furiganapad {

// The ruby notation of note (note.com) and many novel sites: |漢字《かんじ》
ustring to_note_ruby(const ustring& text);

// HTML: <ruby>漢字<rp>(</rp><rt>かんじ</rt><rp>)</rp></ruby>. The other text
// is escaped, and line breaks become <br>.
ustring to_html_ruby(const ustring& text);

// The "HTML Format" clipboard data (CF_HTML) for an HTML fragment, UTF-8.
std::string make_cf_html(const std::string& fragment);

}  // namespace furiganapad
