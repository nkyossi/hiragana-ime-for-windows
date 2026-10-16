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

// Port of breaker.py: cursor positions and word boundaries of a paragraph,
// with the ICU break iterators of Windows (icu.dll).

#include <vector>

#include "Annotation.h"

namespace furiganapad {

class Breaker {
public:
    Breaker() = default;
    explicit Breaker(const ustring& text) {
        if (!text.empty()) set_text(text);
    }

    index_t following(index_t offset) const;
    index_t following_word_end(index_t offset) const;
    index_t preceding(index_t offset) const;
    index_t preceding_word_start(index_t offset) const;
    void set_text(const ustring& text);

private:
    index_t length_ = 0;
    std::vector<char> cursor_positions_;
    std::vector<char> word_starts_;
    std::vector<char> word_ends_;
};

}  // namespace furiganapad
