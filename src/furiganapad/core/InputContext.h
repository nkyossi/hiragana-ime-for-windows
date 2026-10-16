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

// The input method handlers of FuriganaView in textview.py, without the
// drawing: on_commit(), on_delete_surrounding(), on_retrieve_surrounding()
// and the preedit signals. This is where the furigana is added: the reading
// deleted by the IME is kept, and the kanji committed after it gets the
// reading as its ruby.

#include <functional>
#include <vector>

#include "Annotation.h"
#include "TextBuffer.h"

namespace furiganapad {

struct PreeditAttribute {
    enum Style { Input, Candidate, Locked };
    index_t start;   // characters
    index_t length;  // characters
    Style style;
};

struct Preedit {
    ustring text;
    std::vector<PreeditAttribute> attributes;
    index_t cursor = 0;  // characters
};

class InputContext {
public:
    explicit InputContext(FuriganaBuffer& buffer) : buffer_(buffer) {}

    // retrieve-surrounding: the text and the cursor offset in characters.
    std::pair<ustring, index_t> retrieve_surrounding() { return buffer_.get_surrounding(); }
    void delete_surrounding(index_t offset, index_t n_chars);
    void commit(const ustring& text);
    // preedit-start, preedit-changed and preedit-end
    void preedit_changed(const Preedit& preedit);

    // The requests of the IME (IBus.Engine.commit_text() and
    // update_preedit_text()) turned into the signals above as the GTK IM
    // module emits them: preedit-changed for every visible preedit and once
    // when it is hidden. Unlike GTK, the preedit is hidden before a commit so
    // that annotate() always sees the candidate that was shown as the last
    // preedit; with a kana layout no roman letters are shown before a
    // conversion and the first conversion would get no ruby otherwise.
    void ibus_commit_text(const ustring& text);
    void ibus_update_preedit_text(const Preedit& preedit);

    const Preedit& preedit() const { return preedit_; }
    bool has_preedit() const { return !preedit_.text.empty(); }

    // Called by the view, e.g. after a reflow or to scroll to the cursor.
    void set_listener(std::function<void()> on_changed) { on_changed_ = std::move(on_changed); }

    // Exposed for tests.
    const ustring& reading() const { return reading_; }
    bool surround_deleted() const { return surround_deleted_; }
    ustring annotate(const ustring& s, const ustring& r) const;

private:
    void changed() {
        if (on_changed_) on_changed_();
    }

    FuriganaBuffer& buffer_;
    Preedit preedit_;
    ustring last_preedit_;
    ustring reading_;
    bool surround_deleted_ = false;
    std::function<void()> on_changed_;
};

}  // namespace furiganapad
