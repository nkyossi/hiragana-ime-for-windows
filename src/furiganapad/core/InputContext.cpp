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

#include "InputContext.h"

namespace furiganapad {

namespace {

bool starts_with(const ustring& s, const ustring& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

ustring InputContext::annotate(const ustring& s_in, const ustring& r_in) const {
    ustring s = s_in;
    ustring r = r_in;
    if (s.empty()) return ustring();
    if (r.empty() || last_preedit_.empty()) return s;

    ustring before;
    ustring after;
    if (starts_with(s, last_preedit_)) {
        after = s.substr(last_preedit_.size());
        s = last_preedit_;
    }

    index_t start = 0, end = 0;
    if (search_kanzi(s, start, end)) {
        before = s.substr(0, static_cast<size_t>(start));
        if (starts_with(r, before)) r = r.substr(static_cast<size_t>(start));
        after = s.substr(static_cast<size_t>(end)) + after;
        s = ustring(1, IAA) + s.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)) +
            ustring(1, IAS) + r + ustring(1, IAT);
    }
    return before + s + after;
}

void InputContext::commit(const ustring& text_in) {
    ustring text = text_in;
    if (text.empty()) surround_deleted_ = false;
    if (surround_deleted_) {
        if (starts_with(reading_, text)) {
            if (text == reading_) {
                // canceled by [Esc]
                reading_.clear();
            } else if (text.find(U'―') == ustring::npos) {
                ustring reading = reading_.substr(text.size());
                if (is_reading(reading)) {
                    // Shrink reading_
                    // かけ― → か + け―
                    reading_ = reading;
                }
            } else {
                // おだ―や → おだ―
                reading_ = text;
            }
        } else {
            if (buffer_.get_ruby_mode() && !buffer_.get_cursor().inside_ruby()) text = annotate(text, reading_);
            reading_.clear();
            surround_deleted_ = false;
        }
    }

    buffer_.begin_user_action();
    buffer_.insert_at_cursor(text);
    buffer_.end_user_action(surround_deleted_);
    changed();
}

void InputContext::delete_surrounding(index_t offset, index_t n_chars) {
    buffer_.begin_user_action();
    ustring reading = buffer_.delete_surrounding(offset, n_chars);

    if (surround_deleted_) {
        if (!reading_.empty()) {
            if (reading.find(U'―') == ustring::npos) {
                if (is_reading(reading + reading_)) {
                    // Extend reading_
                    // か + け―る
                    reading_ = reading + reading_;
                }
            } else if (is_reading(reading)) {
                reading_ = reading;
            } else {
                reading_.clear();
                surround_deleted_ = false;
            }
        } else {
            surround_deleted_ = false;
        }
    }
    if (!surround_deleted_) {
        if (is_reading(reading)) {
            reading_ = reading;
            surround_deleted_ = true;
        } else {
            reading_.clear();
        }
    }

    buffer_.end_user_action(surround_deleted_);
    changed();
}

void InputContext::ibus_commit_text(const ustring& text) {
    if (has_preedit()) preedit_changed(Preedit());
    commit(text);
}

void InputContext::ibus_update_preedit_text(const Preedit& preedit) {
    if (preedit.text.empty() && !has_preedit()) return;
    preedit_changed(preedit);
}

void InputContext::preedit_changed(const Preedit& preedit) {
    if (!preedit_.text.empty()) last_preedit_ = preedit_.text;
    preedit_ = preedit;
    buffer_.delete_selection(true, true);
    changed();
}

}  // namespace furiganapad
