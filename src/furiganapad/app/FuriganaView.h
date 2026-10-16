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

// Port of FuriganaView in textview.py: a text view that draws rubies above
// the base text, with Direct2D and DirectWrite instead of Pango and Cairo.
//
// Input methods:
// - Hiragana IME talks to the view through DirectClient (see
//   src/common/DirectClient.h), which gives the same requests as IBus, so the
//   furigana is added as in the original.
// - Other IMEs use the IMM32 composition messages; their text is committed
//   without furigana, as with other IBus engines in the original.

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "DirectClient.h"
#include "InputContext.h"
#include "Markdown.h"
#include "TextBuffer.h"

namespace furiganapad {

struct ViewSettings {
    std::wstring font_family = L"BIZ UDGothic";
    float font_size = 16.0f;  // DIPs; 'Noto Sans Mono CJK JP 16px' in the original
    bool highlight_sentences = true;
};

class FuriganaView : public BufferObserver, public hiragana::direct::DirectClient {
public:
    // Called when the cursor moves or the text changes, e.g. for the status bar.
    struct Callbacks {
        std::function<void()> cursor_moved;
        std::function<void()> modified_changed;
    };

    FuriganaView(HWND parent, Callbacks callbacks, const ViewSettings& settings);
    ~FuriganaView() override;
    FuriganaView(const FuriganaView&) = delete;
    FuriganaView& operator=(const FuriganaView&) = delete;

    HWND hwnd() const { return hwnd_; }
    FuriganaBuffer& buffer() { return buffer_; }

    // Actions of the window
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void select_all(bool select);
    void insert_newline();
    void set_font(const std::wstring& family, float size);
    const ViewSettings& settings() const { return settings_; }
    void set_check_sentences(bool value);
    bool get_check_sentences() const { return settings_.highlight_sentences; }
    void set_markdown(bool markdown);
    bool markdown() const { return markdown_; }
    void place_cursor_onscreen();
    void scroll_mark_onscreen(TextMark* mark);
    // Commits what the IME is composing, e.g. before saving.
    void commit_input();

    // BufferObserver
    void on_inserted(const TextIter& iter, const ustring& text) override;
    void on_delete(const TextIter& start, const TextIter& end) override;
    void on_deleted(const TextIter& start, const TextIter& end) override;
    void on_mark_set(const TextIter& where, TextMark* mark) override;
    void on_modified_changed() override;

    // DirectClient
    int __stdcall GetSurroundingText(wchar_t* buffer, int capacity, int* cursor) override;
    void __stdcall DeleteSurroundingText(int offset, int count) override;
    void __stdcall CommitText(const wchar_t* text, int length) override;
    void __stdcall UpdatePreedit(const wchar_t* text, int length, const hiragana::direct::PreeditSpan* spans,
                                 int span_count, int cursor) override;
    BOOL __stdcall GetCaretRect(RECT* rect) override;
    void __stdcall SetHost(hiragana::direct::DirectHost* host) override;

private:
    struct Layout {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        std::wstring text;           // the plain text with the preedit
        std::vector<UINT32> u16;     // character offset -> UTF-16 offset
        float height = 0;
        float scale = 1;             // font size of a heading relative to the base size
        index_t preedit_at = -1;     // plain offset where the preedit is inserted
        index_t preedit_length = 0;  // characters
        bool valid = false;
    };

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);

    // textview.py
    void do_backspace();
    void do_delete_from_cursor();
    enum class Step { LogicalPositions, Words, DisplayLines, ParagraphEnds, Pages, BufferEnds };
    void do_move_cursor(Step step, int count, bool extend_selection);
    std::pair<bool, TextIter> get_iter_at_location(float x, float y);
    void reflow(index_t line = -1, bool redraw = true);

    // Layout and drawing
    void create_device_resources();
    void discard_device_resources();
    void update_metrics();
    Layout& layout_of(index_t line);
    void build_layout(index_t line, Layout& out);
    float paragraph_top(index_t line) const;
    void paint();
    void draw_paragraph(index_t line, float y);
    void draw_rubies(index_t line, const Layout& l, float y);
    void fill_range(const Layout& l, float y, index_t start, index_t end, ID2D1Brush* brush);
    std::vector<std::pair<std::pair<index_t, index_t>, int>> check_sentences(const ustring& text) const;
    void update_caret();
    void update_scrollbar();
    void set_scroll(float offset);
    float dip(int px) const { return px * 96.0f / dpi_; }
    int px(float dip_value) const { return static_cast<int>(dip_value * dpi_ / 96.0f + 0.5f); }
    float view_width() const;   // text area width in DIPs
    float view_height() const;  // client height in DIPs
    void update_markdown_styles();

    // Input methods
    void on_char(wchar_t c);
    void on_ime_composition(LPARAM flags);
    void on_ime_end_composition();
    void on_input_changed();
    void request_commit();
    void request_reset();

    HWND hwnd_ = nullptr;
    Callbacks callbacks_;
    ViewSettings settings_;
    FuriganaBuffer buffer_;
    InputContext input_{buffer_};
    hiragana::direct::DirectHost* host_ = nullptr;
    TextMark* anchor_ = nullptr;
    index_t reflow_line_ = -1;
    int click_count_ = 0;
    bool markdown_ = false;
    std::vector<MarkdownParagraph> markdown_styles_;
    wchar_t high_surrogate_ = 0;
    bool focused_ = false;

    // Metrics in DIPs
    UINT dpi_ = 96;
    float padding_ = 8;
    float ascent_ = 0;
    float line_height_ = 0;  // self.line_height
    float spacing_ = 0;      // self.spacing
    float glyph_height_ = 0; // ascent + descent
    float offset_ = 0;       // vertical scroll position
    float last_width_ = 0;
    int caret_height_ = 0;   // pixels
    std::vector<Layout> layouts_;
    std::vector<float> heights_;
    float height_ = 0;
    D2D1_RECT_F caret_ = {};  // in content coordinates (x relative to the text area)

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selection_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sentence_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> long_sentence_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> candidate_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> locked_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> marker_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> link_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> code_brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> ruby_format_;
};

}  // namespace furiganapad
