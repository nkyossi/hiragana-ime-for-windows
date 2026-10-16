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

#include "TextBuffer.h"

#include <algorithm>
#include <cassert>

namespace furiganapad {

namespace {

enum Mode { PLAIN = 0, BASE = 1, RUBY = 2 };

constexpr index_t MAX_PRECIDING_TEXT = 512;
constexpr index_t MAX_SURROUNDING_TEXT = 1024;

size_t sz(index_t i) { return static_cast<size_t>(i); }

ustring sub(const ustring& s, index_t a, index_t b) {
    index_t n = static_cast<index_t>(s.size());
    a = std::clamp<index_t>(a, 0, n);
    b = std::clamp<index_t>(b, 0, n);
    return a < b ? s.substr(sz(a), sz(b - a)) : ustring();
}

ustring sub(const ustring& s, index_t a) { return sub(s, a, static_cast<index_t>(s.size())); }

// Python str.find(c, start) and str.rfind(c, 0, end) for a character.
index_t find(const ustring& s, char32_t c, index_t start) {
    if (start < 0) start = 0;
    size_t p = s.find(c, sz(start));
    return p == ustring::npos ? -1 : static_cast<index_t>(p);
}

index_t rfind(const ustring& s, char32_t c, index_t end) {
    for (index_t i = std::min<index_t>(end, static_cast<index_t>(s.size())) - 1; 0 <= i; --i) {
        if (s[sz(i)] == c) return i;
    }
    return -1;
}

// str.splitlines(keepends=True)
std::vector<ustring> split_lines(const ustring& text) {
    std::vector<ustring> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t next = 0;
        size_t brk = next_line_break(text, start, next);
        if (brk == ustring::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, next - start));
        start = next;
    }
    return lines;
}

ustring rstrip_newlines(const ustring& s) {
    size_t n = s.size();
    while (0 < n && contains(NEWLINES, s[n - 1])) --n;
    return s.substr(0, n);
}

}  // namespace

//
// Paragraph
//
index_t Paragraph::backward_cursor_position(index_t offset) const { return breaker_.preceding(offset); }

index_t Paragraph::backward_visible_word_start(index_t offset) const { return breaker_.preceding_word_start(offset); }

index_t Paragraph::expand_plain_offset(index_t offset) const {
    Mode mode = PLAIN;
    index_t i = 0;
    index_t length = 0;
    for (char32_t c : text_) {
        if (length == offset) {
            if (i < static_cast<index_t>(text_.size()) && text_[sz(i)] == IAS) i = find(text_, IAT, i + 1) + 1;
            break;
        }
        if (c == IAA)
            mode = BASE;
        else if (c == IAS)
            mode = RUBY;
        else if (c == IAT)
            mode = PLAIN;
        else if (mode == RUBY)
            ;
        else
            length += 1;
        i += 1;
    }
    return i;
}

index_t Paragraph::forward_cursor_position(index_t offset) const { return breaker_.following(offset); }

std::optional<std::pair<index_t, index_t>> Paragraph::forward_search(index_t offset, const ustring& s) const {
    const ustring& plain = get_plain_text();
    offset = get_plain_offset(offset);
    size_t found = plain.find(s, sz(offset));
    if (found == ustring::npos) return std::nullopt;
    index_t start = static_cast<index_t>(found);
    index_t end = expand_plain_offset(start + static_cast<index_t>(s.size()));
    start = expand_plain_offset(start);
    return std::make_pair(start, end);
}

index_t Paragraph::forward_visible_word_end(index_t offset) const { return breaker_.following_word_end(offset); }

index_t Paragraph::get_plain_offset(index_t offset) const {
    Mode mode = PLAIN;
    index_t i = 0;
    index_t pos = 0;
    for (char32_t c : text_) {
        if (i == offset) break;
        if (c == IAA)
            mode = BASE;
        else if (c == IAS)
            mode = RUBY;
        else if (c == IAT)
            mode = PLAIN;
        else if (mode == RUBY)
            ;
        else
            pos += 1;
        i += 1;
    }
    return pos;
}

bool Paragraph::inside_ruby(index_t offset) const {
    for (index_t i = offset; i < static_cast<index_t>(text_.size()); ++i) {
        char32_t c = text_[sz(i)];
        if (c == IAA) return false;
        if (c == IAS) return true;
        if (c == IAT) return true;
    }
    return false;
}

void Paragraph::update_plain_text() {
    plain_.clear();
    rubies_.clear();
    Mode mode = PLAIN;
    index_t i = 0, pos = 0, length = 0;
    ustring ruby;
    for (char32_t c : text_) {
        if (c == IAA) {
            mode = BASE;
            pos = i;
        } else if (c == IAS) {
            mode = RUBY;
            length = i - pos;
        } else if (c == IAT) {
            mode = PLAIN;
            rubies_.push_back({pos, length, ruby});
            ruby.clear();
        } else if (mode == RUBY) {
            ruby += c;
        } else {
            plain_ += c;
            i += 1;
        }
    }
}

void Paragraph::insert(index_t offset, const ustring& text) {
    set_text(sub(text_, 0, offset) + text + sub(text_, offset));
}

void Paragraph::set_text(const ustring& text) {
    text_ = text;
    update_plain_text();
    breaker_ = Breaker(text_);
}

Paragraph Paragraph::split(index_t offset) {
    Paragraph cont(sub(text_, offset));
    set_text(sub(text_, 0, offset));
    return cont;
}

//
// TextIter
//
bool TextIter::backward_cursor_position() { return buffer_->backward_cursor_position_(*this); }

bool TextIter::backward_cursor_positions(index_t count) {
    if (count == 0) return false;
    if (count < 0) return forward_cursor_positions(-count);
    for (index_t i = 0; i < count; ++i) {
        if (!backward_cursor_position()) return false;
    }
    return true;
}

bool TextIter::backward_visible_word_start() { return buffer_->backward_visible_word_start_(*this); }

bool TextIter::backward_visible_word_starts(index_t count) {
    if (count == 0) return false;
    if (count < 0) return forward_visible_word_ends(-count);
    for (index_t i = 0; i < count; ++i) {
        if (!backward_visible_word_start()) return false;
    }
    return true;
}

bool TextIter::forward_chars(index_t count) { return buffer_->forward_chars_(*this, count); }

bool TextIter::forward_cursor_position() { return buffer_->forward_cursor_position_(*this); }

bool TextIter::forward_cursor_positions(index_t count) {
    if (count == 0) return false;
    if (count < 0) return backward_cursor_positions(-count);
    for (index_t i = 0; i < count; ++i) {
        if (!forward_cursor_position()) return false;
    }
    return true;
}

bool TextIter::forward_lines(index_t count) {
    if (line_ + count < buffer_->get_line_count()) {
        line_ += count;
        offset_ = 0;
        return true;
    }
    line_ = buffer_->get_line_count() - 1;
    offset_ = buffer_->paragraph(line_).get_length() - 1;
    return false;
}

std::optional<std::pair<TextIter, TextIter>> TextIter::forward_search(const ustring& s, const TextIter* limit) {
    if (s.empty()) return std::nullopt;
    if (limit && *limit <= *this) return std::nullopt;
    return buffer_->forward_search_(*this, s, limit);
}

bool TextIter::forward_visible_word_end() { return buffer_->forward_visible_word_end_(*this); }

bool TextIter::forward_visible_word_ends(index_t count) {
    if (count == 0) return false;
    if (count < 0) return backward_visible_word_starts(-count);
    for (index_t i = 0; i < count; ++i) {
        if (!forward_visible_word_end()) return false;
    }
    return true;
}

index_t TextIter::get_plain_line_offset() const { return buffer_->get_plain_line_offset_(*this); }

bool TextIter::inside_ruby() const { return buffer_->inside_ruby_(*this); }

//
// TextMark
//
void TextMark::on_delete(const TextIter& start, const TextIter& end) {
    if (start == end || iter <= start) return;
    if (iter <= end) {
        iter = start;
    } else {
        index_t d = iter.get_line() - end.get_line();
        iter.set_line(start.get_line() + d);
        if (d <= 0) {
            d = iter.get_line_offset() - end.get_line_offset();
            iter.set_line_offset(start.get_line_offset() + d);
        }
    }
    buffer_->emit_mark_set(iter, this);
}

void TextMark::on_insert(const TextIter& start, const TextIter& end) {
    if (start == end || iter < start) return;
    if (iter == start) {
        iter = end;
    } else {
        index_t d = iter.get_line() - start.get_line();
        iter.set_line(end.get_line() + d);
        if (d <= 0) {
            d = iter.get_line_offset() - start.get_line_offset();
            iter.set_line_offset(end.get_line_offset() + d);
        }
    }
    buffer_->emit_mark_set(iter, this);
}

//
// FuriganaBuffer
//
FuriganaBuffer::FuriganaBuffer() {
    // TextBuffer always contains at least one line
    paragraphs_.emplace_back();
    TextIter iter = get_start_iter();
    create_mark("insert", iter);
    create_mark("selection_bound", iter);
}

void FuriganaBuffer::emit_mark_set(const TextIter& where, TextMark* mark) {
    for (auto* o : observers_) o->on_mark_set(where, mark);
}

void FuriganaBuffer::emit_modified_changed() {
    for (auto* o : observers_) o->on_modified_changed();
}

bool FuriganaBuffer::backward_cursor_position_(TextIter& iter) {
    index_t lineno = iter.get_line();
    index_t offset = paragraphs_[sz(lineno)].backward_cursor_position(iter.get_line_offset());
    if (0 <= offset) {
        iter.set_line_offset(offset);
        return true;
    }
    lineno -= 1;
    if (0 <= lineno) {
        iter.set_line(lineno);
        iter.set_line_offset(paragraphs_[sz(lineno)].get_length() - 1);
        return true;
    }
    return false;
}

bool FuriganaBuffer::backward_visible_word_start_(TextIter& iter) {
    index_t lineno = iter.get_line();
    index_t offset = paragraphs_[sz(lineno)].backward_visible_word_start(iter.get_line_offset());
    if (0 <= offset) {
        iter.set_line_offset(offset);
        return true;
    }
    lineno -= 1;
    if (0 <= lineno) {
        iter.set_line(lineno);
        iter.set_line_offset(paragraphs_[sz(lineno)].get_length() - 1);
        return true;
    }
    return false;
}

bool FuriganaBuffer::empty() const {
    if (1 < get_line_count()) return false;
    return paragraphs_[0].get_length() == 1;
}

bool FuriganaBuffer::forward_chars_(TextIter& iter, index_t count) {
    if (count == 0) return false;
    index_t lineno = iter.get_line();
    index_t offset = iter.get_line_offset() + count;
    for (;;) {
        index_t length = paragraphs_[sz(lineno)].get_length();
        if (0 <= offset && offset < length) {
            iter.set_line(lineno);
            iter.set_line_offset(offset);
            return true;
        }
        if (length <= offset) {
            lineno += 1;
            if (static_cast<index_t>(paragraphs_.size()) <= lineno) lineno = 0;
            offset -= length;
        } else {
            lineno -= 1;
            if (lineno < 0) lineno = static_cast<index_t>(paragraphs_.size()) - 1;
            offset += paragraphs_[sz(lineno)].get_length();
        }
    }
}

bool FuriganaBuffer::forward_cursor_position_(TextIter& iter) {
    index_t lineno = iter.get_line();
    index_t offset = paragraphs_[sz(lineno)].forward_cursor_position(iter.get_line_offset());
    if (offset < paragraphs_[sz(lineno)].get_length()) {
        iter.set_line_offset(offset);
        return true;
    }
    lineno += 1;
    if (lineno < static_cast<index_t>(paragraphs_.size())) {
        iter.set_line(lineno);
        iter.set_line_offset(0);
        return true;
    }
    return false;
}

bool FuriganaBuffer::forward_visible_word_end_(TextIter& iter) {
    index_t lineno = iter.get_line();
    index_t offset = paragraphs_[sz(lineno)].forward_visible_word_end(iter.get_line_offset());
    if (offset < paragraphs_[sz(lineno)].get_length()) {
        iter.set_line_offset(offset);
        return true;
    }
    lineno += 1;
    if (lineno < static_cast<index_t>(paragraphs_.size())) {
        iter.set_line(lineno);
        iter.set_line_offset(0);
        return true;
    }
    return false;
}

std::optional<std::pair<TextIter, TextIter>> FuriganaBuffer::forward_search_(TextIter iter, const ustring& s,
                                                                             const TextIter* limit) {
    for (;;) {
        auto bounds = paragraphs_[sz(iter.get_line())].forward_search(iter.get_line_offset(), s);
        if (bounds) {
            iter.set_line_offset(bounds->first);
            TextIter end = iter;
            end.set_line_offset(bounds->second);
            if (!limit || end <= *limit) return std::make_pair(iter, end);
            return std::nullopt;
        }
        if (!iter.forward_lines(1)) return std::nullopt;
        if (limit && *limit <= iter) return std::nullopt;
    }
}

index_t FuriganaBuffer::get_plain_line_offset_(const TextIter& iter) const {
    return paragraphs_[sz(iter.get_line())].get_plain_offset(iter.get_line_offset());
}

bool FuriganaBuffer::inside_ruby_(const TextIter& iter) const {
    return paragraphs_[sz(iter.get_line())].inside_ruby(iter.get_line_offset());
}

void FuriganaBuffer::begin_user_action() { user_action_ = true; }

std::optional<ustring> FuriganaBuffer::copy_clipboard() {
    auto [start, end] = get_selection_bounds();
    if (start == end) return std::nullopt;
    return remove_dangling_annotations(get_text(start, end, true));
}

std::optional<ustring> FuriganaBuffer::cut_clipboard() {
    auto [start, end] = get_selection_bounds();
    if (start == end) return std::nullopt;
    ustring text = remove_dangling_annotations(get_text(start, end, true));
    delete_selection(true, true);
    return text;
}

TextMark* FuriganaBuffer::create_mark(const std::string& name, const TextIter& where) {
    auto mark = std::make_unique<TextMark>(this, name, where);
    TextMark* p = mark.get();
    marks_[name] = std::move(mark);
    return p;
}

void FuriganaBuffer::delete_(TextIter& start_arg, TextIter& end_arg) {
    if (start_arg == end_arg) return;
    TextIter* sp = &start_arg;
    TextIter* ep = &end_arg;
    if (*ep < *sp) std::swap(sp, ep);
    TextIter& start = *sp;
    TextIter& end = *ep;

    // Check annotation before 'start'
    annotated_.clear();
    TextIter cursor = start;
    {
        const ustring& text = paragraphs_[sz(start.get_line())].get_text();
        index_t offset = start.get_line_offset();
        index_t pos = offset;
        char32_t d = 0;
        index_t i = 0;
        for (index_t k = pos; k < static_cast<index_t>(text.size()); ++k) {
            char32_t c = text[sz(k)];
            if (c == IAA) {
                break;
            } else if (c == IAS) {
                d = IAA;
                break;
            } else if (c == IAT) {
                d = IAS;
                break;
            }
            i += 1;
            cursor.set_line_offset(pos + i);
            if (end <= cursor) break;
        }
        if (d == IAS) {
            pos = rfind(text, IAS, pos);
            d = IAA;
        }
        if (d == IAA) {
            offset = rfind(text, IAA, pos);
            annotated_ = sub(text, offset + 1, pos);
        }
        start.set_line_offset(offset);
    }

    // Check annotation after 'end'
    cursor = end;
    {
        const ustring& text = paragraphs_[sz(end.get_line())].get_text();
        index_t offset = end.get_line_offset();
        index_t pos = offset;
        char32_t d = 0;
        index_t i = 0;
        for (index_t k = std::min<index_t>(pos, static_cast<index_t>(text.size())) - 1; 0 <= k; --k) {
            char32_t c = text[sz(k)];
            if (c == IAA) {
                d = IAS;
                break;
            } else if (c == IAS) {
                d = IAT;
                break;
            } else if (c == IAT) {
                break;
            }
            i -= 1;
            cursor.set_line_offset(pos + i);
            if (cursor <= start) break;
        }
        if (d == IAS) {
            offset = find(text, IAS, pos);
            annotated_ += sub(text, pos, offset);
            pos = offset;
            d = IAT;
        }
        if (d == IAT) offset = find(text, IAT, pos) + 1;
        end.set_line_offset(offset);
    }

    for (auto& [name, mark] : marks_) mark->on_delete(start, end);

    bool was_modified = get_modified();
    emit_delete_range(start, end);
    if (!was_modified) emit_modified_changed();
}

void FuriganaBuffer::emit_delete_range(TextIter& start, TextIter& end) {
    // on_delete (undo), the observers, do_delete_range, then the observers again.
    if (user_action_) {
        Action action{false, start.get_line(), start.get_line_offset(), end.get_line(), end.get_line_offset(),
                      get_text(start, end, true), std::nullopt};
        undo_.push_back(action);
        redo_.clear();
    }
    for (auto* o : observers_) o->on_delete(start, end);
    do_delete_range(start, end);
    for (auto* o : observers_) o->on_deleted(start, end);
}

bool FuriganaBuffer::delete_selection(bool interactive, bool) {
    if (!get_has_selection()) return false;
    TextIter anchor = get_anchor();
    TextIter cursor = get_cursor();
    if (interactive) {
        begin_user_action();
        delete_(anchor, cursor);
        end_user_action();
    } else {
        delete_(anchor, cursor);
    }
    return true;
}

ustring FuriganaBuffer::delete_surrounding(index_t offset, index_t length) {
    if (length == 0) return ustring();
    TextIter start = get_cursor();
    start.forward_cursor_positions(offset);
    TextIter end = start;
    end.forward_cursor_positions(length);
    if (end < start) std::swap(start, end);
    if (start.get_line() != end.get_line()) return ustring();
    ustring deleted = sub(paragraphs_[sz(start.get_line())].get_text(), start.get_line_offset(), end.get_line_offset());
    delete_(start, end);
    return deleted;
}

void FuriganaBuffer::do_delete_range(TextIter& start, TextIter& end) {
    if (start.get_line() == end.get_line()) {
        const ustring& text = paragraphs_[sz(start.get_line())].get_text();
        ustring updated = sub(text, 0, start.get_line_offset()) + sub(text, end.get_line_offset());
        paragraphs_[sz(start.get_line())].set_text(updated);
        end.set_line_offset(start.get_line_offset());
    } else {
        index_t lineno = end.get_line();
        if (start.get_line() + 1 < lineno) {
            paragraphs_.erase(paragraphs_.begin() + start.get_line() + 1, paragraphs_.begin() + lineno);
            lineno = start.get_line() + 1;
        }
        ustring text = sub(paragraphs_[sz(lineno)].get_text(), end.get_line_offset());
        paragraphs_.erase(paragraphs_.begin() + lineno);
        lineno -= 1;
        text = sub(paragraphs_[sz(lineno)].get_text(), 0, start.get_line_offset()) + text;
        paragraphs_[sz(lineno)].set_text(text);
        end.set_line(lineno);
        end.set_line_offset(start.get_line_offset());
    }

    // Insert annotated text again without the annotation
    if (!annotated_.empty()) {
        ustring annotated = annotated_;
        annotated_.clear();
        TextIter at = start;
        insert(at, annotated);
    }
}

void FuriganaBuffer::do_insert_text(TextIter& iter, const ustring& text) {
    std::vector<ustring> lines = split_lines(text);
    index_t lineno = iter.get_line();
    if (lines.size() == 1 && !contains(NEWLINES, lines[0].back())) {
        paragraphs_[sz(lineno)].insert(iter.get_line_offset(), text);
        iter.set_line_offset(iter.get_line_offset() + static_cast<index_t>(text.size()));
        return;
    }

    Paragraph cont = paragraphs_[sz(lineno)].split(iter.get_line_offset());
    if (lineno == get_line_count() - 1)
        paragraphs_.push_back(cont);
    else
        paragraphs_.insert(paragraphs_.begin() + lineno + 1, cont);

    paragraphs_[sz(lineno)].insert(iter.get_line_offset(), rstrip_newlines(lines[0]));
    lines.erase(lines.begin());
    if (lines.empty()) {
        iter.set_line(lineno + 1);
        iter.set_line_offset(0);
        return;
    }

    for (const ustring& s : lines) {
        lineno += 1;
        if (!contains(NEWLINES, s.back())) {
            paragraphs_[sz(lineno)].insert(0, s);
            iter.set_line(lineno);
            iter.set_line_offset(static_cast<index_t>(s.size()));
            return;
        }
        paragraphs_.insert(paragraphs_.begin() + lineno, Paragraph(rstrip_newlines(s)));
    }
    iter.set_line(lineno + 1);
    iter.set_line_offset(0);
}

void FuriganaBuffer::do_redo() {
    if (redo_.empty()) return;
    bool was_modified = get_modified();
    Action action = redo_.back();
    redo_.pop_back();
    TextIter start;
    if (action.insert) {
        start = get_iter_at_line_offset(action.line1, action.offset1);
        TextIter at = start;
        insert(at, action.text);
    } else {
        start = get_iter_at_line_offset(action.line1, action.offset1);
        TextIter end = get_iter_at_line_offset(action.line2, action.offset2);
        delete_(start, end);
        // Redo previous actions that were issued by ibus-hiragana
        if (action.surround_deleted.value_or(false)) {
            while (!redo_.empty()) {
                undo_.push_back(action);
                action = redo_.back();
                redo_.pop_back();
                if (action.insert) {
                    start = get_iter_at_line_offset(action.line1, action.offset1);
                    TextIter at = start;
                    insert(at, action.text);
                } else {
                    start = get_iter_at_line_offset(action.line1, action.offset1);
                    TextIter e = get_iter_at_line_offset(action.line2, action.offset2);
                    delete_(start, e);
                }
                if (!action.surround_deleted.value_or(false)) break;
            }
        }
    }
    place_cursor(start);
    undo_.push_back(action);
    if (!was_modified) emit_modified_changed();
}

void FuriganaBuffer::do_undo() {
    if (!get_modified()) return;
    Action action = undo_.back();
    undo_.pop_back();
    TextIter start;
    if (action.insert) {
        start = get_iter_at_line_offset(action.line1, action.offset1);
        TextIter end = get_iter_at_line_offset(action.line2, action.offset2);
        delete_(start, end);
        // Undo previous actions that were issued by ibus-hiragana
        while (!undo_.empty()) {
            const Action& prev = undo_.back();
            if (!prev.surround_deleted.value_or(false)) break;
            redo_.push_back(action);
            bool prev_is_delete = !prev.insert;
            action = undo_.back();
            undo_.pop_back();
            if (prev_is_delete) {
                start = get_iter_at_line_offset(action.line1, action.offset1);
                TextIter at = start;
                insert(at, action.text);
            } else {
                start = get_iter_at_line_offset(action.line1, action.offset1);
                TextIter e = get_iter_at_line_offset(action.line2, action.offset2);
                delete_(start, e);
            }
        }
    } else {
        start = get_iter_at_line_offset(action.line1, action.offset1);
        TextIter at = start;
        insert(at, action.text);
    }
    place_cursor(start);
    redo_.push_back(action);
    if (!get_modified()) emit_modified_changed();
}

void FuriganaBuffer::end_user_action(std::optional<bool> surround_deleted) {
    if (surround_deleted.has_value() && !undo_.empty()) undo_.back().surround_deleted = surround_deleted;
    user_action_ = false;
}

TextIter FuriganaBuffer::get_end_iter() {
    index_t lineno = get_line_count() - 1;
    return TextIter(this, lineno, paragraphs_[sz(lineno)].get_length() - 1);
}

bool FuriganaBuffer::get_has_selection() const { return get_selection_bound()->iter != get_insert()->iter; }

TextIter FuriganaBuffer::get_iter_at_line_offset(index_t line, index_t offset) {
    line = std::clamp<index_t>(line, 0, get_line_count() - 1);
    offset = std::clamp<index_t>(offset, 0, paragraphs_[sz(line)].get_length() - 1);
    return TextIter(this, line, offset);
}

std::pair<TextIter, TextIter> FuriganaBuffer::get_selection_bounds() {
    TextIter start = get_anchor();
    TextIter end = get_cursor();
    if (end < start) std::swap(start, end);
    return {start, end};
}

std::pair<ustring, index_t> FuriganaBuffer::get_surrounding() {
    TextIter cursor = get_cursor();
    index_t line = cursor.get_line();
    ustring text = paragraphs_[sz(line)].get_text();
    index_t offset = cursor.get_line_offset();
    while (0 < line && offset < MAX_PRECIDING_TEXT && static_cast<index_t>(text.size()) < MAX_SURROUNDING_TEXT) {
        line -= 1;
        const ustring& prev = paragraphs_[sz(line)].get_text();
        text = prev + U"\n" + text;
        offset += static_cast<index_t>(prev.size()) + 1;
    }
    return {text, offset};
}

ustring FuriganaBuffer::get_text(TextIter start, TextIter end, bool include_hidden_chars) {
    if (start == end) return ustring();
    if (end < start) std::swap(start, end);
    index_t line = start.get_line();
    auto text_of = [&](index_t l) -> const ustring& {
        return include_hidden_chars ? paragraphs_[sz(l)].get_text() : paragraphs_[sz(l)].get_plain_text();
    };
    auto offset_of = [&](const TextIter& it) {
        return include_hidden_chars ? it.get_line_offset() : get_plain_line_offset_(it);
    };
    if (line == end.get_line()) return sub(text_of(line), offset_of(start), offset_of(end));
    ustring text = sub(text_of(line), offset_of(start)) + U"\n";
    line += 1;
    while (line < end.get_line()) {
        text += text_of(line) + U"\n";
        line += 1;
    }
    if (line < get_line_count()) text += sub(text_of(line), 0, offset_of(end));
    return text;
}

void FuriganaBuffer::insert(TextIter& iter, const ustring& text_in) {
    if (text_in.empty()) return;
    ustring text = text_in;
    if (empty()) {
        text = rstrip_newlines(text);
        if (text.empty()) text = U"\n";
    }
    bool was_modified = get_modified();
    TextIter start = iter;
    emit_insert_text(iter, text);
    for (auto& [name, mark] : marks_) mark->on_insert(start, iter);
    if (!was_modified) emit_modified_changed();
}

void FuriganaBuffer::emit_insert_text(TextIter& iter, const ustring& text) {
    if (user_action_) inserting_ = iter;
    do_insert_text(iter, text);
    if (user_action_ && inserting_) {
        Action action{true, inserting_->get_line(), inserting_->get_line_offset(), iter.get_line(),
                      iter.get_line_offset(), text, std::nullopt};
        undo_.push_back(action);
        redo_.clear();
        inserting_.reset();
    }
    for (auto* o : observers_) o->on_inserted(iter, text);
}

void FuriganaBuffer::insert_at_cursor(const ustring& text) {
    delete_selection(false, true);
    TextIter cursor = get_cursor();
    insert(cursor, text);
}

void FuriganaBuffer::move_cursor(const TextIter& cursor, bool is_selection) {
    if (is_selection)
        move_mark(get_insert(), cursor);
    else
        place_cursor(cursor);
}

void FuriganaBuffer::move_mark(TextMark* mark, const TextIter& where) {
    mark->iter = where;
    emit_mark_set(where, mark);
}

void FuriganaBuffer::select_all() {
    auto [start, end] = get_bounds();
    select_range(start, end);
}

void FuriganaBuffer::select_range(const TextIter& ins, const TextIter& bound) {
    move_mark(get_insert(), ins);
    move_mark(get_selection_bound(), bound);
}

bool FuriganaBuffer::set_modified(bool modified) {
    bool current = get_modified();
    if (current && !modified) {
        undo_.clear();
        redo_.clear();
    }
    if (current != get_modified()) emit_modified_changed();
    return get_modified();
}

void FuriganaBuffer::set_text(const ustring& text) {
    auto [start, end] = get_bounds();
    delete_(start, end);
    insert(start, text);
    place_cursor(get_start_iter());
}

bool FuriganaBuffer::unconvert(const TextIter& iter) {
    const ustring text = paragraphs_[sz(iter.get_line())].get_text();
    index_t pos = iter.get_line_offset();
    if (pos < 5 || text[sz(pos - 1)] != IAT) return false;
    char32_t mode = IAT;
    ustring ruby;
    index_t size = 1;
    for (index_t k = pos - 2; 0 <= k; --k) {
        char32_t c = text[sz(k)];
        size += 1;
        if (c == IAA) {
            TextIter start = iter;
            start.set_line_offset(pos - size);
            TextIter end = iter;
            delete_(start, end);
            insert(end, ruby);
            return true;
        } else if (c == IAS) {
            mode = IAS;
        } else if (mode == IAT) {
            ruby = ustring(1, c) + ruby;
        }
    }
    return false;
}

}  // namespace furiganapad
