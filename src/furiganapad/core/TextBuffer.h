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

// Port of textbuffer.py. The GObject signals of FuriganaBuffer are delivered
// to BufferObserver in the same order as the handlers connected in the
// original: the buffer's own handlers first, then the observers.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Annotation.h"
#include "Breaker.h"

namespace furiganapad {

class FuriganaBuffer;

struct Ruby {
    index_t pos;     // offset in the plain text
    index_t length;  // length of the base text
    ustring ruby;
};

class Paragraph {
public:
    explicit Paragraph(const ustring& text = ustring()) { set_text(text); }

    index_t backward_cursor_position(index_t offset) const;
    index_t backward_visible_word_start(index_t offset) const;
    index_t expand_plain_offset(index_t offset) const;
    index_t forward_cursor_position(index_t offset) const;
    std::optional<std::pair<index_t, index_t>> forward_search(index_t offset, const ustring& sub) const;
    index_t forward_visible_word_end(index_t offset) const;
    index_t get_plain_offset(index_t offset) const;
    bool inside_ruby(index_t offset) const;
    // plus one as a newline at the end of the line
    index_t get_length() const { return static_cast<index_t>(text_.size()) + 1; }
    index_t get_plain_length() const { return static_cast<index_t>(plain_.size()) + 1; }
    const ustring& get_plain_text() const { return plain_; }
    const ustring& get_text() const { return text_; }
    const std::vector<Ruby>& rubies() const { return rubies_; }
    void insert(index_t offset, const ustring& text);
    void set_text(const ustring& text);
    Paragraph split(index_t offset);

private:
    void update_plain_text();

    ustring text_;
    ustring plain_;
    std::vector<Ruby> rubies_;
    Breaker breaker_;
};

class TextIter {
public:
    TextIter() = default;
    TextIter(FuriganaBuffer* buffer, index_t line, index_t offset) : buffer_(buffer), line_(line), offset_(offset) {}

    bool operator==(const TextIter& o) const { return line_ == o.line_ && offset_ == o.offset_; }
    bool operator!=(const TextIter& o) const { return !(*this == o); }
    bool operator<(const TextIter& o) const { return line_ < o.line_ || (line_ == o.line_ && offset_ < o.offset_); }
    bool operator<=(const TextIter& o) const { return !(o < *this); }
    bool operator>(const TextIter& o) const { return o < *this; }
    bool operator>=(const TextIter& o) const { return !(*this < o); }

    bool backward_char() { return forward_chars(-1); }
    bool backward_chars(index_t count) { return forward_chars(-count); }
    bool backward_cursor_position();
    bool backward_cursor_positions(index_t count);
    bool backward_visible_word_start();
    bool backward_visible_word_starts(index_t count);
    TextIter copy() const { return *this; }
    bool forward_char() { return forward_chars(1); }
    bool forward_chars(index_t count);
    bool forward_cursor_position();
    bool forward_cursor_positions(index_t count);
    bool forward_lines(index_t count);
    std::optional<std::pair<TextIter, TextIter>> forward_search(const ustring& sub, const TextIter* limit);
    bool forward_visible_word_end();
    bool forward_visible_word_ends(index_t count);
    FuriganaBuffer* get_buffer() const { return buffer_; }
    index_t get_line() const { return line_; }
    index_t get_line_offset() const { return offset_; }
    index_t get_plain_line_offset() const;
    bool inside_ruby() const;
    void set_line(index_t line) { line_ = line; }
    void set_line_offset(index_t offset) { offset_ = offset; }

private:
    FuriganaBuffer* buffer_ = nullptr;
    index_t line_ = 0;
    index_t offset_ = 0;
};

class TextMark {
public:
    TextMark(FuriganaBuffer* buffer, std::string name, const TextIter& iter)
        : buffer_(buffer), name_(std::move(name)), iter(iter) {}
    const std::string& get_name() const { return name_; }
    void on_delete(const TextIter& start, const TextIter& end);
    // The cursor has moved from start to end.
    void on_insert(const TextIter& start, const TextIter& end);

private:
    FuriganaBuffer* buffer_;
    std::string name_;

public:
    TextIter iter;
};

class BufferObserver {
public:
    virtual ~BufferObserver() = default;
    virtual void on_inserted(const TextIter& iter, const ustring& text) {}
    virtual void on_delete(const TextIter& start, const TextIter& end) {}
    virtual void on_deleted(const TextIter& start, const TextIter& end) {}
    virtual void on_mark_set(const TextIter& where, TextMark* mark) {}
    virtual void on_modified_changed() {}
};

class FuriganaBuffer {
public:
    FuriganaBuffer();
    FuriganaBuffer(const FuriganaBuffer&) = delete;
    FuriganaBuffer& operator=(const FuriganaBuffer&) = delete;

    void add_observer(BufferObserver* observer) { observers_.push_back(observer); }

    bool empty() const;
    void begin_user_action();
    void end_user_action(std::optional<bool> surround_deleted = std::nullopt);
    // Returns the text for the clipboard, or nothing when there is no selection.
    std::optional<ustring> copy_clipboard();
    std::optional<ustring> cut_clipboard();
    TextMark* create_mark(const std::string& name, const TextIter& where);
    // Like textbuffer.py, start and end are updated: start to the start of the
    // deleted range and end to the position after the deletion.
    void delete_(TextIter& start, TextIter& end);
    void delete_range(TextIter start, TextIter end) { delete_(start, end); }
    bool delete_selection(bool interactive, bool default_editable);
    ustring delete_surrounding(index_t offset, index_t length);
    void do_redo();
    void do_undo();
    TextIter get_anchor() { return get_iter_at_mark(get_selection_bound()); }
    std::pair<TextIter, TextIter> get_bounds() { return {get_start_iter(), get_end_iter()}; }
    TextIter get_cursor() { return get_iter_at_mark(get_insert()); }
    TextIter get_end_iter();
    bool get_has_selection() const;
    TextMark* get_insert() const { return marks_.at("insert").get(); }
    TextIter get_iter_at_line_offset(index_t line, index_t offset);
    TextIter get_iter_at_mark(const TextMark* mark) const { return mark->iter; }
    index_t get_line_count() const { return static_cast<index_t>(paragraphs_.size()); }
    bool get_modified() const { return !undo_.empty(); }
    bool get_ruby_mode() const { return ruby_mode_; }
    TextMark* get_selection_bound() const { return marks_.at("selection_bound").get(); }
    std::pair<TextIter, TextIter> get_selection_bounds();
    TextIter get_start_iter() { return TextIter(this, 0, 0); }
    std::pair<ustring, index_t> get_surrounding();
    ustring get_text(TextIter start, TextIter end, bool include_hidden_chars);
    void insert(TextIter& iter, const ustring& text);
    void insert_at_cursor(const ustring& text);
    void move_cursor(const TextIter& cursor, bool is_selection);
    void move_mark(TextMark* mark, const TextIter& where);
    void place_cursor(const TextIter& where) { select_range(where, where); }
    void select_all();
    void select_range(const TextIter& ins, const TextIter& bound);
    bool set_modified(bool modified);
    void set_ruby_mode(bool ruby) { ruby_mode_ = ruby; }
    void set_text(const ustring& text);
    bool unconvert(const TextIter& iter);

    const Paragraph& paragraph(index_t line) const { return paragraphs_[static_cast<size_t>(line)]; }
    const std::vector<Paragraph>& paragraphs() const { return paragraphs_; }

    // Used by TextIter
    bool backward_cursor_position_(TextIter& iter);
    bool backward_visible_word_start_(TextIter& iter);
    bool forward_chars_(TextIter& iter, index_t count);
    bool forward_cursor_position_(TextIter& iter);
    bool forward_visible_word_end_(TextIter& iter);
    std::optional<std::pair<TextIter, TextIter>> forward_search_(TextIter iter, const ustring& sub,
                                                                 const TextIter* limit);
    index_t get_plain_line_offset_(const TextIter& iter) const;
    bool inside_ruby_(const TextIter& iter) const;
    void emit_mark_set(const TextIter& where, TextMark* mark);

private:
    struct Action {
        bool insert;  // 'insert-text' or 'delete-range'
        index_t line1, offset1, line2, offset2;
        ustring text;
        std::optional<bool> surround_deleted;  // action[7]
    };

    void do_delete_range(TextIter& start, TextIter& end);
    void do_insert_text(TextIter& iter, const ustring& text);
    void emit_delete_range(TextIter& start, TextIter& end);
    void emit_insert_text(TextIter& iter, const ustring& text);
    void emit_modified_changed();

    std::map<std::string, std::unique_ptr<TextMark>> marks_;
    std::vector<Paragraph> paragraphs_;
    ustring annotated_;
    bool ruby_mode_ = true;

    // Undo buffers
    bool user_action_ = false;
    std::vector<Action> undo_;
    std::vector<Action> redo_;
    std::optional<TextIter> inserting_;

    std::vector<BufferObserver*> observers_;
};

}  // namespace furiganapad
