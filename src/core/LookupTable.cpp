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

#include "LookupTable.h"

namespace hiragana {

// These follow ibus_lookup_table_cursor_up() and friends in ibus/src/ibuslookuptable.c
// with round set to FALSE.

bool LookupTable::cursor_up() {
    if (cursor_pos_ == 0) return false;
    --cursor_pos_;
    return true;
}

bool LookupTable::cursor_down() {
    if (candidates_.empty() || cursor_pos_ + 1 >= candidates_.size()) return false;
    ++cursor_pos_;
    return true;
}

bool LookupTable::page_up() {
    if (cursor_pos_ < page_size_) return false;
    cursor_pos_ -= page_size_;
    return true;
}

bool LookupTable::page_down() {
    if (candidates_.empty()) return false;
    size_t page = cursor_pos_ / page_size_;
    size_t pages = (candidates_.size() + page_size_ - 1) / page_size_;
    if (page + 1 >= pages) return false;
    cursor_pos_ += page_size_;
    if (cursor_pos_ >= candidates_.size()) cursor_pos_ = candidates_.size() - 1;
    return true;
}

}  // namespace hiragana
