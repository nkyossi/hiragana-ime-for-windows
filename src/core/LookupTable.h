// Hiragana IME for Windows
//
// Copyright (c) 2026 Hiragana IME for Windows contributors
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

// The subset of IBus.LookupTable used by the engine (round = false).

#include <vector>

#include "Text.h"

namespace hiragana {

class LookupTable {
public:
    explicit LookupTable(size_t page_size = 10) : page_size_(page_size) {}

    void clear() {
        candidates_.clear();
        cursor_pos_ = 0;
    }
    void append_candidate(const ustring& text) { candidates_.push_back(text); }
    size_t get_number_of_candidates() const { return candidates_.size(); }
    const ustring& get_candidate(size_t index) const { return candidates_[index]; }
    const std::vector<ustring>& candidates() const { return candidates_; }
    size_t get_cursor_pos() const { return cursor_pos_; }
    void set_cursor_pos(size_t pos) { cursor_pos_ = pos < candidates_.size() ? pos : 0; }
    size_t get_page_size() const { return page_size_; }
    // Index of the first candidate on the current page.
    size_t page_start() const { return cursor_pos_ - cursor_pos_ % page_size_; }

    bool cursor_up();
    bool cursor_down();
    bool page_up();
    bool page_down();

private:
    std::vector<ustring> candidates_;
    size_t page_size_;
    size_t cursor_pos_ = 0;
};

}  // namespace hiragana
