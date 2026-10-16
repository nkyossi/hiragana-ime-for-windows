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

#include "FuriganaView.h"

#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace furiganapad {

namespace {

const wchar_t kClassName[] = L"FuriganaView";

// A sentence with more than SENTENCE_SHORT characters is not short.
constexpr index_t SENTENCE_SHORT = 50;
// A sentence with more than SENTENCE_LONG characters is long.
constexpr index_t SENTENCE_LONG = 60;
// 2.75 in the original; the usual ruby of half the size is easier to read on
// screens with 100% scaling.
constexpr float RUBY_DIV = 2.0f;

ID2D1Factory* d2d() {
    static ComPtr<ID2D1Factory> factory;
    if (!factory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    return factory.Get();
}

IDWriteFactory* dwrite() {
    static ComPtr<IDWriteFactory> factory;
    if (!factory)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory.Get();
}

UINT client_message() {
    static const UINT message = RegisterWindowMessageW(hiragana::direct::kGetClientMessage);
    return message;
}

UINT window_dpi(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static auto get_dpi = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    UINT dpi = get_dpi ? get_dpi(hwnd) : 0;
    if (!dpi) {
        HDC dc = GetDC(nullptr);
        dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
        ReleaseDC(nullptr, dc);
    }
    return dpi ? dpi : 96;
}

float heading_scale(int level) {
    switch (level) {
        case 1: return 1.8f;
        case 2: return 1.5f;
        case 3: return 1.25f;
        case 4: return 1.1f;
        default: return 1.0f;
    }
}

bool same_style(const MarkdownParagraph& a, const MarkdownParagraph& b) {
    if (a.heading != b.heading || a.code_block != b.code_block || a.quote != b.quote || a.rule != b.rule ||
        a.spans.size() != b.spans.size())
        return false;
    for (size_t i = 0; i < a.spans.size(); ++i) {
        if (a.spans[i].start != b.spans[i].start || a.spans[i].end != b.spans[i].end ||
            a.spans[i].kind != b.spans[i].kind)
            return false;
    }
    return true;
}

bool find_font(const std::wstring& family) {
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(dwrite()->GetSystemFontCollection(&fonts))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    fonts->FindFamilyName(family.c_str(), &index, &exists);
    return exists != FALSE;
}

}  // namespace

FuriganaView::FuriganaView(HWND parent, Callbacks callbacks, const ViewSettings& settings)
    : callbacks_(std::move(callbacks)), settings_(settings) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof wc};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    buffer_.add_observer(this);
    anchor_ = buffer_.create_mark("anchor", buffer_.get_start_iter());
    input_.set_listener([this] { on_input_changed(); });
    hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, 0, 0, 100, 100,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    dpi_ = window_dpi(hwnd_);
    update_metrics();
}

FuriganaView::~FuriganaView() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

LRESULT CALLBACK FuriganaView::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* view = static_cast<FuriganaView*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        view->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
    }
    auto* view = reinterpret_cast<FuriganaView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (view) return view->handle(msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

//
// Metrics and layout
//
void FuriganaView::update_metrics() {
    if (!find_font(settings_.font_family)) {
        for (const wchar_t* fallback : {L"BIZ UDGothic", L"Yu Gothic UI", L"Meiryo", L"MS Gothic"}) {
            if (find_font(fallback)) {
                settings_.font_family = fallback;
                break;
            }
        }
    }
    float size = settings_.font_size;
    format_.Reset();
    ruby_format_.Reset();
    dwrite()->CreateTextFormat(settings_.font_family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ja-jp", &format_);
    dwrite()->CreateTextFormat(settings_.font_family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size / RUBY_DIV, L"ja-jp",
                               &ruby_format_);
    if (format_) format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (ruby_format_) ruby_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // The ascent and descent of the font
    float ascent = size * 0.88f;
    float descent = size * 0.12f;
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(dwrite()->GetSystemFontCollection(&fonts)) &&
        SUCCEEDED(fonts->FindFamilyName(settings_.font_family.c_str(), &index, &exists)) && exists) {
        ComPtr<IDWriteFontFamily> family;
        ComPtr<IDWriteFont> font;
        if (SUCCEEDED(fonts->GetFontFamily(index, &family)) &&
            SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS m;
            font->GetMetrics(&m);
            ascent = size * m.ascent / m.designUnitsPerEm;
            descent = size * m.descent / m.designUnitsPerEm;
        }
    }
    ascent_ = ascent;
    glyph_height_ = ascent + descent;
    line_height_ = std::ceil(glyph_height_ * 1.6f);
    spacing_ = std::ceil(glyph_height_ * 0.6f);
    reflow();
}

float FuriganaView::view_width() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return (std::max)(1.0f, dip(rc.right) - padding_ * 2);
}

float FuriganaView::view_height() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return dip(rc.bottom);
}

FuriganaView::Layout& FuriganaView::layout_of(index_t line) {
    if (static_cast<index_t>(layouts_.size()) != buffer_.get_line_count()) {
        layouts_.assign(static_cast<size_t>(buffer_.get_line_count()), Layout());
    }
    Layout& l = layouts_[static_cast<size_t>(line)];
    if (!l.valid) build_layout(line, l);
    return l;
}

void FuriganaView::build_layout(index_t line, Layout& out) {
    out = Layout();
    const Paragraph& paragraph = buffer_.paragraph(line);
    ustring text = paragraph.get_plain_text();
    TextIter cursor = buffer_.get_cursor();
    if (line == cursor.get_line() && input_.has_preedit()) {
        index_t at = cursor.get_plain_line_offset();
        out.preedit_at = at;
        out.preedit_length = static_cast<index_t>(input_.preedit().text.size());
        text = text.substr(0, static_cast<size_t>(at)) + input_.preedit().text + text.substr(static_cast<size_t>(at));
    }
    out.text = to_wide(text);
    out.u16.resize(text.size() + 1);
    UINT32 n = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        out.u16[i] = n;
        n += text[i] < 0x10000 ? 1 : 2;
    }
    out.u16[text.size()] = n;

    const MarkdownParagraph* md = nullptr;
    if (markdown_ && line < static_cast<index_t>(markdown_styles_.size()))
        md = &markdown_styles_[static_cast<size_t>(line)];
    out.scale = md ? heading_scale(md->heading) : 1.0f;

    if (!format_) return;
    dwrite()->CreateTextLayout(out.text.c_str(), static_cast<UINT32>(out.text.size()), format_.Get(), view_width(),
                               1.0e6f, &out.layout);
    if (!out.layout) return;
    UINT32 total = static_cast<UINT32>(out.text.size());
    auto range = [&](index_t start, index_t end) {
        start = std::clamp<index_t>(start, 0, static_cast<index_t>(text.size()));
        end = std::clamp<index_t>(end, start, static_cast<index_t>(text.size()));
        return DWRITE_TEXT_RANGE{out.u16[static_cast<size_t>(start)],
                                 out.u16[static_cast<size_t>(end)] - out.u16[static_cast<size_t>(start)]};
    };
    // Plain offset -> display offset (after the preedit when it is at or before the offset)
    auto display = [&](index_t offset) {
        return (0 <= out.preedit_at && out.preedit_at <= offset) ? offset + out.preedit_length : offset;
    };
    if (out.scale != 1.0f) {
        out.layout->SetFontSize(settings_.font_size * out.scale, {0, total});
        out.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, {0, total});
    }
    out.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, (glyph_height_ + spacing_) * out.scale,
                               (spacing_ + ascent_) * out.scale);
    if (md) {
        for (const auto& span : md->spans) {
            DWRITE_TEXT_RANGE r = range(display(span.start), display(span.end));
            if (r.length == 0) continue;
            switch (span.kind) {
                case MarkdownSpan::Marker:
                    if (marker_brush_) out.layout->SetDrawingEffect(marker_brush_.Get(), r);
                    break;
                case MarkdownSpan::Bold:
                    out.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                    break;
                case MarkdownSpan::Italic:
                    out.layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                    break;
                case MarkdownSpan::Strike:
                    out.layout->SetStrikethrough(TRUE, r);
                    break;
                case MarkdownSpan::Link:
                    if (link_brush_) out.layout->SetDrawingEffect(link_brush_.Get(), r);
                    out.layout->SetUnderline(TRUE, r);
                    break;
                case MarkdownSpan::Code:
                    break;  // background in draw_paragraph()
            }
        }
    }
    if (0 <= out.preedit_at) {
        for (const auto& a : input_.preedit().attributes) {
            DWRITE_TEXT_RANGE r = range(out.preedit_at + a.start, out.preedit_at + a.start + a.length);
            if (a.style == PreeditAttribute::Locked) {
                if (locked_brush_) out.layout->SetDrawingEffect(locked_brush_.Get(), r);
            } else {
                out.layout->SetUnderline(TRUE, r);
            }
        }
        if (input_.preedit().attributes.empty()) out.layout->SetUnderline(TRUE, range(out.preedit_at, display(out.preedit_at)));
    }
    DWRITE_TEXT_METRICS metrics;
    out.layout->GetMetrics(&metrics);
    out.height = (std::max)(metrics.height, (glyph_height_ + spacing_) * out.scale);
    out.valid = true;
}

void FuriganaView::update_markdown_styles() {
    if (!markdown_) {
        markdown_styles_.clear();
        return;
    }
    std::vector<ustring> plain;
    plain.reserve(static_cast<size_t>(buffer_.get_line_count()));
    for (const auto& p : buffer_.paragraphs()) plain.push_back(p.get_plain_text());
    std::vector<MarkdownParagraph> styles = markdown_styles(plain);
    // Paragraphs whose style has changed, e.g. after a code fence was added, need new layouts.
    if (layouts_.size() == styles.size()) {
        for (size_t i = 0; i < styles.size(); ++i) {
            if (i >= markdown_styles_.size() || !same_style(styles[i], markdown_styles_[i])) layouts_[i].valid = false;
        }
    }
    markdown_styles_ = std::move(styles);
}

void FuriganaView::reflow(index_t line, bool redraw) {
    float width = view_width();
    if (std::fabs(width - last_width_) > 0.01f) {
        last_width_ = width;
        line = -1;
    }
    if (static_cast<index_t>(layouts_.size()) != buffer_.get_line_count() ||
        static_cast<index_t>(heights_.size()) != buffer_.get_line_count())
        line = -1;

    if (0 <= line && line < buffer_.get_line_count()) {
        layouts_[static_cast<size_t>(line)].valid = false;
    } else {
        layouts_.assign(static_cast<size_t>(buffer_.get_line_count()), Layout());
    }
    update_markdown_styles();

    heights_.resize(static_cast<size_t>(buffer_.get_line_count()));
    height_ = 0;
    for (index_t i = 0; i < buffer_.get_line_count(); ++i) {
        Layout& l = layout_of(i);
        heights_[static_cast<size_t>(i)] = l.height;
        height_ += l.height;
    }
    height_ += spacing_;
    update_scrollbar();
    if (redraw && hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

float FuriganaView::paragraph_top(index_t line) const {
    float y = 0;
    for (index_t i = 0; i < line && i < static_cast<index_t>(heights_.size()); ++i) y += heights_[static_cast<size_t>(i)];
    return y;
}

//
// Drawing
//
void FuriganaView::create_device_resources() {
    if (target_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = static_cast<float>(dpi_);
    d2d()->CreateHwndRenderTarget(props,
                                  D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(static_cast<UINT32>(rc.right),
                                                                                      static_cast<UINT32>(rc.bottom))),
                                  &target_);
    if (!target_) return;
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &text_brush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0xac / 255.0f, 0xce / 255.0f, 0xf7 / 255.0f), &selection_brush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 0xa0 / 255.0f), &sentence_brush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0xa0 / 255.0f, 0xa0 / 255.0f), &long_sentence_brush_);
    // CANDIDATE_BACKGROUND_COLOR and PREFIX_LOCK_COLOR of ibus-hiragana
    target_->CreateSolidColorBrush(D2D1::ColorF(0xd1eaff), &candidate_brush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0x3399ff), &locked_brush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0x9a9a9a), &marker_brush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0x0b62c4), &link_brush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0xeeeeee), &code_brush_);
    // The layouts refer to the brushes.
    for (auto& l : layouts_) l.valid = false;
}

void FuriganaView::discard_device_resources() {
    target_.Reset();
    text_brush_.Reset();
    selection_brush_.Reset();
    sentence_brush_.Reset();
    long_sentence_brush_.Reset();
    candidate_brush_.Reset();
    locked_brush_.Reset();
    marker_brush_.Reset();
    link_brush_.Reset();
    code_brush_.Reset();
}

void FuriganaView::fill_range(const Layout& l, float y, index_t start, index_t end, ID2D1Brush* brush) {
    if (!l.layout || !brush) return;
    index_t n = static_cast<index_t>(l.u16.size()) - 1;
    start = std::clamp<index_t>(start, 0, n);
    end = std::clamp<index_t>(end, start, n);
    if (start == end) return;
    UINT32 a = l.u16[static_cast<size_t>(start)];
    UINT32 b = l.u16[static_cast<size_t>(end)];
    UINT32 count = 0;
    l.layout->HitTestTextRange(a, b - a, padding_, y, nullptr, 0, &count);
    if (count == 0) return;
    std::vector<DWRITE_HIT_TEST_METRICS> rects(count);
    l.layout->HitTestTextRange(a, b - a, padding_, y, rects.data(), count, &count);
    // With the uniform line spacing, top is the top of the glyphs; the space for
    // the rubies is above it.
    for (const auto& m : rects) target_->FillRectangle(D2D1::RectF(m.left, m.top, m.left + m.width, m.top + m.height), brush);
}

// _check_sentences(): the ranges of long sentences; the int is 1 for a not short sentence, 2 for a long one.
std::vector<std::pair<std::pair<index_t, index_t>, int>> FuriganaView::check_sentences(const ustring& text) const {
    std::vector<std::pair<std::pair<index_t, index_t>, int>> result;
    if (!settings_.highlight_sentences) return result;
    index_t markup = 0;  // length of the text before the current sentence
    index_t sentence = 0;
    index_t start = 0, end = 0;
    index_t n = static_cast<index_t>(text.size());
    const ustring_view blanks = U"\t 　";
    const ustring_view stops = U"　 。．？！";
    for (index_t i = 0; i < n; ++i) {
        char32_t c = text[static_cast<size_t>(i)];
        if (start == end) {
            if (contains(blanks, c)) {
                markup += 1;
                start += 1;
                end = start;
                continue;
            }
        }
        end = i + 1;
        if (contains(stops, c) || n <= end) {
            if (c == U'　' || c == U' ')
                end -= 1;
            else
                sentence += 1;
            index_t count = end - start;
            if (SENTENCE_SHORT < count) result.push_back({{markup, markup + sentence}, SENTENCE_LONG < count ? 2 : 1});
            markup += sentence;
            if (c == U'　' || c == U' ') markup += 1;
            start = end = i + 1;
            sentence = 0;
        } else {
            sentence += 1;
        }
    }
    return result;
}

void FuriganaView::draw_paragraph(index_t line, float y) {
    Layout& l = layout_of(line);
    if (!l.layout) return;
    auto [start, end] = buffer_.get_selection_bounds();
    ustring text = from_wide(l.text);

    if (start == end || line < start.get_line() || end.get_line() < line) {
        for (const auto& [r, level] : check_sentences(text))
            fill_range(l, y, r.first, r.second, level == 2 ? long_sentence_brush_.Get() : sentence_brush_.Get());
    } else {
        index_t so = 0;
        index_t eo = static_cast<index_t>(text.size());
        if (start.get_line() == line) so = start.get_plain_line_offset();
        if (end.get_line() == line) eo = end.get_plain_line_offset();
        fill_range(l, y, so, eo, selection_brush_.Get());
    }
    if (0 <= l.preedit_at) {
        for (const auto& a : input_.preedit().attributes) {
            if (a.style == PreeditAttribute::Candidate)
                fill_range(l, y, l.preedit_at + a.start, l.preedit_at + a.start + a.length, candidate_brush_.Get());
        }
    }
    if (markdown_ && line < static_cast<index_t>(markdown_styles_.size())) {
        const auto& md = markdown_styles_[static_cast<size_t>(line)];
        auto display = [&](index_t o) { return (0 <= l.preedit_at && l.preedit_at <= o) ? o + l.preedit_length : o; };
        for (const auto& span : md.spans) {
            if (span.kind == MarkdownSpan::Code) fill_range(l, y, display(span.start), display(span.end), code_brush_.Get());
        }
        if (md.quote) {
            float top = y + spacing_ * l.scale;
            target_->FillRectangle(D2D1::RectF(1, top, 4, y + l.height), marker_brush_.Get());
        }
        if (md.rule) {
            float mid = y + spacing_ + glyph_height_ / 2;
            target_->DrawLine(D2D1::Point2F(padding_, mid), D2D1::Point2F(padding_ + view_width(), mid),
                              marker_brush_.Get(), 1.0f);
        }
    }
    target_->DrawTextLayout(D2D1::Point2F(padding_, y), l.layout.Get(), text_brush_.Get());
    draw_rubies(line, l, y);
}

void FuriganaView::draw_rubies(index_t line, const Layout& l, float y) {
    const Paragraph& paragraph = buffer_.paragraph(line);
    float width = view_width();
    for (const Ruby& r : paragraph.rubies()) {
        if (r.length <= 0 || r.ruby.empty()) continue;
        index_t pos = r.pos;
        if (0 <= l.preedit_at && l.preedit_at <= pos) pos += l.preedit_length;
        index_t last = pos + r.length - 1;
        if (static_cast<size_t>(last) + 1 >= l.u16.size()) continue;
        FLOAT lx, ly, rx, ry;
        DWRITE_HIT_TEST_METRICS left, right;
        l.layout->HitTestTextPosition(l.u16[static_cast<size_t>(pos)], FALSE, &lx, &ly, &left);
        l.layout->HitTestTextPosition(l.u16[static_cast<size_t>(last)], FALSE, &rx, &ry, &right);
        float left_x = left.left;
        float right_x = right.left + right.width;

        auto draw = [&](const ustring& text, float x_hint, float top, int align) {
            // align: 0 centered between left_x and right_x, 1 flush right, -1 flush left
            std::wstring w = to_wide(text);
            ComPtr<IDWriteTextLayout> rl;
            dwrite()->CreateTextLayout(w.c_str(), static_cast<UINT32>(w.size()), ruby_format_.Get(), 1.0e6f, 1.0e6f, &rl);
            if (!rl) return;
            if (l.scale != 1.0f) rl->SetFontSize(settings_.font_size * l.scale / RUBY_DIV, {0, static_cast<UINT32>(w.size())});
            DWRITE_TEXT_METRICS m;
            rl->GetMetrics(&m);
            float x = x_hint;
            if (align == 0) {
                x = (left_x + right_x - m.width) / 2;
                if (x < 0)
                    x = 0;
                else if (width < x + m.width)
                    x = width - m.width;
            } else if (align == 1) {
                x = width - m.width;
            }
            float ruby_y = y + top - m.height;
            target_->DrawTextLayout(D2D1::Point2F(padding_ + x, ruby_y), rl.Get(), text_brush_.Get());
            return;
        };

        if (left.top == right.top) {
            draw(r.ruby, 0, left.top, 0);
        } else {
            float ruby_width = right_x + width - left_x;
            index_t left_length = static_cast<index_t>(
                std::lround(static_cast<double>(r.ruby.size()) * (width - left_x) / ruby_width));
            if (0 < left_length) draw(r.ruby.substr(0, static_cast<size_t>(left_length)), 0, left.top, 1);
            if (left_length < static_cast<index_t>(r.ruby.size()))
                draw(r.ruby.substr(static_cast<size_t>(left_length)), 0, right.top, -1);
        }
    }
}

void FuriganaView::paint() {
    create_device_resources();
    if (!target_) return;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(D2D1::ColorF::White));
    float height = view_height();
    float y = -offset_;
    for (index_t i = 0; i < buffer_.get_line_count(); ++i) {
        float h = layout_of(i).height;
        if (y < height && 0 <= y + h) draw_paragraph(i, y);
        y += h;
    }
    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) discard_device_resources();
    update_caret();
}

void FuriganaView::update_caret() {
    TextIter cursor = buffer_.get_cursor();
    index_t line = cursor.get_line();
    Layout& l = layout_of(line);
    float x = 0, top = 0;
    if (l.layout) {
        index_t offset = cursor.get_plain_line_offset();
        if (0 <= l.preedit_at) offset += input_.preedit().cursor;
        offset = std::clamp<index_t>(offset, 0, static_cast<index_t>(l.u16.size()) - 1);
        FLOAT px_, py_;
        DWRITE_HIT_TEST_METRICS m;
        l.layout->HitTestTextPosition(l.u16[static_cast<size_t>(offset)], FALSE, &px_, &py_, &m);
        x = m.left;
        top = m.top;
    }
    float y = paragraph_top(line) + top;
    caret_ = D2D1::RectF(x - 1, y, x + 1, y + glyph_height_ * l.scale);
    if (focused_) {
        int h = px(glyph_height_ * l.scale);
        if (h != caret_height_) {
            DestroyCaret();
            CreateCaret(hwnd_, nullptr, 2, h);
            caret_height_ = h;
            ShowCaret(hwnd_);
        }
        SetCaretPos(px(padding_ + caret_.left), px(caret_.top - offset_));
    }
}

//
// Scrolling
//
void FuriganaView::update_scrollbar() {
    if (!hwnd_) return;
    float height = view_height();
    float upper = (std::max)(height_, height);
    if (upper - height < offset_) offset_ = (std::max)(0.0f, upper - height);
    SCROLLINFO si = {sizeof si};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = static_cast<int>(std::ceil(upper));
    si.nPage = static_cast<UINT>(height);
    si.nPos = static_cast<int>(offset_);
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void FuriganaView::set_scroll(float offset) {
    float upper = (std::max)(height_, view_height());
    offset_ = std::clamp(offset, 0.0f, (std::max)(0.0f, upper - view_height()));
    update_scrollbar();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FuriganaView::place_cursor_onscreen() { scroll_mark_onscreen(buffer_.get_insert()); }

void FuriganaView::scroll_mark_onscreen(TextMark* mark) {
    float height = view_height();
    float offset = offset_;
    index_t line = mark->iter.get_line();
    float y = paragraph_top(line);
    Layout& l = layout_of(line);
    if (l.layout) {
        index_t o = mark->iter.get_plain_line_offset();
        o = std::clamp<index_t>(o, 0, static_cast<index_t>(l.u16.size()) - 1);
        FLOAT px_, py_;
        DWRITE_HIT_TEST_METRICS m;
        l.layout->HitTestTextPosition(l.u16[static_cast<size_t>(o)], FALSE, &px_, &py_, &m);
        y += m.top;
    }
    float upper = (std::max)(height_, height);
    if (offset <= y && y + line_height_ <= offset + height && offset + height <= upper) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (y < offset) {
        // Scroll up
        if (upper < y + height) {
            float lines = std::floor((y + height - upper) / line_height_) + 1;
            y = (std::max)(0.0f, y - lines * line_height_);
        }
    } else {
        // Scroll down
        float lines = std::floor(height / line_height_) - 1;
        y = (std::max)(0.0f, y - lines * line_height_);
    }
    set_scroll(y);
}

//
// Editing, after textview.py
//
void FuriganaView::do_backspace() {
    if (buffer_.delete_selection(true, true)) {
        place_cursor_onscreen();
        return;
    }
    TextIter end = buffer_.get_cursor();
    TextIter start = end;
    if (start.backward_cursor_position()) {
        buffer_.begin_user_action();
        buffer_.delete_(start, end);
        buffer_.end_user_action();
        place_cursor_onscreen();
    }
}

void FuriganaView::do_delete_from_cursor() {
    if (buffer_.delete_selection(true, true)) {
        place_cursor_onscreen();
        return;
    }
    TextIter start = buffer_.get_cursor();
    TextIter end = start;
    if (end.forward_cursor_position()) {
        buffer_.begin_user_action();
        buffer_.delete_(start, end);
        buffer_.end_user_action();
        buffer_.place_cursor(start);
        place_cursor_onscreen();
    }
}

void FuriganaView::do_move_cursor(Step step, int count, bool extend_selection) {
    update_caret();
    switch (step) {
        case Step::LogicalPositions: {
            auto [start, end] = buffer_.get_selection_bounds();
            if (start == end || extend_selection) {
                TextIter cursor = buffer_.get_cursor();
                if (count < 0 && cursor.backward_cursor_positions(-count))  // Left
                    buffer_.move_cursor(cursor, extend_selection);
                else if (0 < count && cursor.forward_cursor_positions(count))  // Right
                    buffer_.move_cursor(cursor, extend_selection);
            } else {
                buffer_.move_cursor(count < 0 ? start : end, extend_selection);
            }
            break;
        }
        case Step::Words: {
            TextIter cursor = buffer_.get_cursor();
            if (count < 0 && cursor.backward_visible_word_starts(-count))
                buffer_.move_cursor(cursor, extend_selection);
            else if (0 < count && cursor.forward_visible_word_ends(count))
                buffer_.move_cursor(cursor, extend_selection);
            break;
        }
        case Step::DisplayLines:
            if (count < 0) {  // Up
                float y = caret_.top - line_height_;
                if (0 <= y) buffer_.move_cursor(get_iter_at_location(caret_.left + 1, y).second, extend_selection);
            } else if (0 < count) {  // Down
                float y = caret_.top + line_height_;
                if (y < height_) buffer_.move_cursor(get_iter_at_location(caret_.left + 1, y).second, extend_selection);
            }
            break;
        case Step::ParagraphEnds:
            if (count < 0) {  // Home
                TextIter cursor = get_iter_at_location(0, caret_.top).second;
                if (cursor != buffer_.get_cursor()) buffer_.move_cursor(cursor, extend_selection);
            } else if (0 < count) {  // End
                TextIter cursor = get_iter_at_location(view_width(), caret_.top).second;
                if (cursor != buffer_.get_cursor()) buffer_.move_cursor(cursor, extend_selection);
            }
            break;
        case Step::Pages: {
            float offset = offset_;
            float height = view_height();
            float y = caret_.top;
            if (count < 0) {  // Page_Up
                offset -= height;
                if (offset < 0) return do_move_cursor(Step::BufferEnds, -1, extend_selection);
                y -= height;
            } else if (0 < count) {  // Page_Down
                offset += height;
                float upper = (std::max)(height_, height);
                if (upper <= offset) return do_move_cursor(Step::BufferEnds, 1, extend_selection);
                y += height;
            }
            set_scroll(offset);
            buffer_.move_cursor(get_iter_at_location(caret_.left + 1, y).second, extend_selection);
            break;
        }
        case Step::BufferEnds:
            if (count < 0) {  // Control-Home
                TextIter cursor = buffer_.get_start_iter();
                if (cursor != buffer_.get_cursor()) buffer_.move_cursor(cursor, extend_selection);
            } else if (0 < count) {  // Control-End
                TextIter cursor = buffer_.get_end_iter();
                if (cursor != buffer_.get_cursor()) buffer_.move_cursor(cursor, extend_selection);
            }
            break;
    }
    place_cursor_onscreen();
}

std::pair<bool, TextIter> FuriganaView::get_iter_at_location(float x, float y) {
    float height = 0;
    for (index_t i = 0; i < buffer_.get_line_count(); ++i) {
        Layout& l = layout_of(i);
        float h = l.height;
        if (y < h + height) {
            const Paragraph& paragraph = buffer_.paragraph(i);
            index_t offset = static_cast<index_t>(paragraph.get_plain_text().size());
            BOOL trailing = FALSE, inside = FALSE;
            if (l.layout) {
                DWRITE_HIT_TEST_METRICS m;
                l.layout->HitTestPoint(x, y - height, &trailing, &inside, &m);
                auto it = std::lower_bound(l.u16.begin(), l.u16.end(), m.textPosition);
                offset = static_cast<index_t>(it - l.u16.begin());
            }
            if (0 <= l.preedit_at && l.preedit_at <= offset) offset = (std::max)(l.preedit_at, offset - l.preedit_length);
            offset = paragraph.expand_plain_offset(offset);
            if (trailing) offset = paragraph.forward_cursor_position(offset);
            offset = (std::min)(offset, paragraph.get_length() - 1);
            return {inside != FALSE, buffer_.get_iter_at_line_offset(i, offset)};
        }
        height += h;
    }
    return {false, buffer_.get_end_iter()};
}

void FuriganaView::undo() {
    request_reset();
    buffer_.do_undo();
    place_cursor_onscreen();
}

void FuriganaView::redo() {
    buffer_.do_redo();
    place_cursor_onscreen();
}

static void set_clipboard(HWND hwnd, const ustring& text) {
    std::wstring w;
    for (wchar_t c : to_wide(text)) {
        if (c == L'\n') w += L'\r';
        w += c;
    }
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (mem) {
        void* p = GlobalLock(mem);
        memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

void FuriganaView::cut() {
    request_commit();
    if (auto text = buffer_.cut_clipboard()) set_clipboard(hwnd_, *text);
    place_cursor_onscreen();
}

void FuriganaView::copy() {
    request_commit();
    if (auto text = buffer_.copy_clipboard()) set_clipboard(hwnd_, *text);
}

void FuriganaView::paste() {
    request_commit();
    if (!OpenClipboard(hwnd_)) return;
    std::wstring w;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            w = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    if (w.empty()) return;
    ustring text = remove_dangling_annotations(from_wide(w));
    buffer_.begin_user_action();
    buffer_.insert_at_cursor(text);
    buffer_.end_user_action();
    place_cursor_onscreen();
}

void FuriganaView::select_all(bool select) {
    if (select) {
        buffer_.select_all();
    } else {
        buffer_.place_cursor(buffer_.get_cursor());
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FuriganaView::insert_newline() {
    buffer_.begin_user_action();
    buffer_.insert_at_cursor(U"\n");
    buffer_.end_user_action();
    place_cursor_onscreen();
}

void FuriganaView::set_font(const std::wstring& family, float size) {
    settings_.font_family = family;
    settings_.font_size = size;
    update_metrics();
    place_cursor_onscreen();
}

void FuriganaView::set_check_sentences(bool value) {
    if (value != settings_.highlight_sentences) {
        settings_.highlight_sentences = value;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void FuriganaView::set_markdown(bool markdown) {
    if (markdown_ == markdown) return;
    markdown_ = markdown;
    markdown_styles_.clear();
    reflow();
}

void FuriganaView::commit_input() { request_commit(); }

//
// BufferObserver
//
void FuriganaView::on_inserted(const TextIter& iter, const ustring& text) {
    if (has_newline(text))
        reflow();
    else
        reflow(iter.get_line());
}

void FuriganaView::on_delete(const TextIter& start, const TextIter& end) {
    reflow_line_ = start.get_line() == end.get_line() ? start.get_line() : -1;
}

void FuriganaView::on_deleted(const TextIter&, const TextIter&) { reflow(reflow_line_); }

void FuriganaView::on_mark_set(const TextIter&, TextMark* mark) {
    if (mark == buffer_.get_insert() || mark == buffer_.get_selection_bound()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (mark == buffer_.get_insert() && callbacks_.cursor_moved) callbacks_.cursor_moved();
    }
}

void FuriganaView::on_modified_changed() {
    if (callbacks_.modified_changed) callbacks_.modified_changed();
}

//
// Input methods
//
void FuriganaView::on_input_changed() {
    reflow(buffer_.get_cursor().get_line());
    place_cursor_onscreen();
}

void FuriganaView::request_commit() {
    if (host_) host_->CommitAll(this);
    if (HIMC himc = ImmGetContext(hwnd_)) {
        ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
        ImmReleaseContext(hwnd_, himc);
    }
}

void FuriganaView::request_reset() {
    if (host_) host_->Reset(this);
}

void FuriganaView::on_char(wchar_t c) {
    if (0xD800 <= c && c <= 0xDBFF) {
        high_surrogate_ = c;
        return;
    }
    std::wstring w;
    if (0xDC00 <= c && c <= 0xDFFF) {
        if (!high_surrogate_) return;
        w += high_surrogate_;
    }
    high_surrogate_ = 0;
    w += c;
    // The GTK simple input method commits the character.
    input_.commit(from_wide(w));
}

void FuriganaView::on_ime_composition(LPARAM flags) {
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;
    if (flags & GCS_RESULTSTR) {
        LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
        std::wstring w(static_cast<size_t>((std::max)(0L, bytes)) / sizeof(wchar_t), L'\0');
        if (bytes > 0) ImmGetCompositionStringW(himc, GCS_RESULTSTR, w.data(), static_cast<DWORD>(bytes));
        if (!w.empty()) input_.ibus_commit_text(from_wide(w));
    }
    if (flags & GCS_COMPSTR) {
        LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
        std::wstring w(static_cast<size_t>((std::max)(0L, bytes)) / sizeof(wchar_t), L'\0');
        if (bytes > 0) ImmGetCompositionStringW(himc, GCS_COMPSTR, w.data(), static_cast<DWORD>(bytes));
        std::vector<BYTE> attrs;
        if (flags & GCS_COMPATTR) {
            LONG n = ImmGetCompositionStringW(himc, GCS_COMPATTR, nullptr, 0);
            if (n > 0) {
                attrs.resize(static_cast<size_t>(n));
                ImmGetCompositionStringW(himc, GCS_COMPATTR, attrs.data(), static_cast<DWORD>(n));
            }
        }
        LONG cursor16 = static_cast<LONG>(w.size());
        if (flags & GCS_CURSORPOS) cursor16 = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
        Preedit preedit;
        preedit.text = from_wide(w);
        // Attributes per UTF-16 unit -> spans in characters
        index_t ch = 0;
        for (size_t i = 0; i < w.size(); ++i) {
            bool low = 0xDC00 <= w[i] && w[i] <= 0xDFFF && i > 0;
            if (low) continue;
            BYTE a = i < attrs.size() ? attrs[i] : ATTR_INPUT;
            PreeditAttribute::Style style =
                (a == ATTR_TARGET_CONVERTED || a == ATTR_TARGET_NOTCONVERTED) ? PreeditAttribute::Candidate
                                                                              : PreeditAttribute::Input;
            if (!preedit.attributes.empty() && preedit.attributes.back().style == style &&
                preedit.attributes.back().start + preedit.attributes.back().length == ch)
                preedit.attributes.back().length += 1;
            else
                preedit.attributes.push_back({ch, 1, style});
            ++ch;
        }
        preedit.cursor = static_cast<index_t>(from_wide(std::wstring_view(w.data(), (std::min)(w.size(), static_cast<size_t>((std::max)(0L, cursor16))))).size());
        input_.ibus_update_preedit_text(preedit);
    }
    // Put the candidate window of the IME below the caret.
    update_caret();
    CANDIDATEFORM cf = {0, CFS_EXCLUDE, {px(padding_ + caret_.left), px(caret_.bottom - offset_)},
                        {px(padding_ + caret_.left), px(caret_.top - offset_), px(padding_ + caret_.right),
                         px(caret_.bottom - offset_)}};
    ImmSetCandidateWindow(himc, &cf);
    ImmReleaseContext(hwnd_, himc);
}

void FuriganaView::on_ime_end_composition() { input_.ibus_update_preedit_text(Preedit()); }

int __stdcall FuriganaView::GetSurroundingText(wchar_t* out, int capacity, int* cursor) {
    auto [text, offset] = input_.retrieve_surrounding();
    std::wstring before = to_wide(text.substr(0, static_cast<size_t>(offset)));
    std::wstring after = to_wide(text.substr(static_cast<size_t>(offset)));
    // Keep the text before the cursor when the buffer is too small.
    if (static_cast<int>(before.size()) >= capacity) before = before.substr(before.size() - static_cast<size_t>(capacity) + 1);
    std::wstring w = before + after;
    if (static_cast<int>(w.size()) >= capacity) w.resize(static_cast<size_t>(capacity) - 1);
    memcpy(out, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    if (cursor) *cursor = static_cast<int>(before.size());
    return static_cast<int>(w.size());
}

void __stdcall FuriganaView::DeleteSurroundingText(int offset, int count) { input_.delete_surrounding(offset, count); }

void __stdcall FuriganaView::CommitText(const wchar_t* text, int length) {
    input_.ibus_commit_text(from_wide(std::wstring_view(text, static_cast<size_t>(length))));
}

void __stdcall FuriganaView::UpdatePreedit(const wchar_t* text, int length, const hiragana::direct::PreeditSpan* spans,
                                           int span_count, int cursor) {
    std::wstring_view w(text, static_cast<size_t>(length));
    auto chars = [&](int u16) {
        return static_cast<index_t>(from_wide(w.substr(0, static_cast<size_t>(std::clamp(u16, 0, length)))).size());
    };
    Preedit preedit;
    preedit.text = from_wide(w);
    for (int i = 0; i < span_count; ++i) {
        index_t start = chars(spans[i].start);
        index_t end = chars(spans[i].start + spans[i].length);
        PreeditAttribute::Style style = spans[i].style == hiragana::direct::kCandidate ? PreeditAttribute::Candidate
                                        : spans[i].style == hiragana::direct::kLocked  ? PreeditAttribute::Locked
                                                                                       : PreeditAttribute::Input;
        preedit.attributes.push_back({start, end - start, style});
    }
    preedit.cursor = chars(cursor);
    input_.ibus_update_preedit_text(preedit);
}

BOOL __stdcall FuriganaView::GetCaretRect(RECT* rect) {
    if (!rect) return FALSE;
    update_caret();
    POINT top_left = {px(padding_ + caret_.left), px(caret_.top - offset_)};
    POINT bottom_right = {px(padding_ + caret_.right), px(caret_.bottom - offset_)};
    ClientToScreen(hwnd_, &top_left);
    ClientToScreen(hwnd_, &bottom_right);
    *rect = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    return TRUE;
}

void __stdcall FuriganaView::SetHost(hiragana::direct::DirectHost* host) { host_ = host; }

//
// Window messages
//
LRESULT FuriganaView::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == client_message() && client_message() != 0) {
        // Hiragana IME asks for the direct connection; lparam is its host.
        host_ = reinterpret_cast<hiragana::direct::DirectHost*>(lparam);
        if (static_cast<int>(wparam) != hiragana::direct::kProtocolVersion) return 0;
        return reinterpret_cast<LRESULT>(static_cast<hiragana::direct::DirectClient*>(this));
    }
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            paint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_GETTEXTLENGTH:
        case WM_GETTEXT: {
            // The text with the ruby annotations, e.g. for tests and screen readers.
            auto [start, end] = buffer_.get_bounds();
            std::wstring w = to_wide(buffer_.get_text(start, end, true));
            if (msg == WM_GETTEXTLENGTH) return static_cast<LRESULT>(w.size());
            size_t capacity = static_cast<size_t>(wparam);
            if (capacity == 0) return 0;
            size_t n = (std::min)(w.size(), capacity - 1);
            memcpy(reinterpret_cast<wchar_t*>(lparam), w.c_str(), n * sizeof(wchar_t));
            reinterpret_cast<wchar_t*>(lparam)[n] = 0;
            return static_cast<LRESULT>(n);
        }
        case WM_SIZE:
            if (target_) target_->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
            reflow();
            return 0;
        case WM_DPICHANGED_AFTERPARENT:
            dpi_ = window_dpi(hwnd_);
            discard_device_resources();
            reflow();
            return 0;
        case WM_SETFOCUS:
            focused_ = true;
            caret_height_ = px(glyph_height_);
            CreateCaret(hwnd_, nullptr, 2, caret_height_);
            ShowCaret(hwnd_);
            update_caret();
            return 0;
        case WM_KILLFOCUS:
            request_commit();
            focused_ = false;
            DestroyCaret();
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
        case WM_KEYDOWN: {
            bool shift = GetKeyState(VK_SHIFT) < 0;
            bool control = GetKeyState(VK_CONTROL) < 0;
            switch (wparam) {
                case VK_BACK: do_backspace(); return 0;
                case VK_DELETE: do_delete_from_cursor(); return 0;
                case VK_LEFT: do_move_cursor(control ? Step::Words : Step::LogicalPositions, -1, shift); return 0;
                case VK_RIGHT: do_move_cursor(control ? Step::Words : Step::LogicalPositions, 1, shift); return 0;
                case VK_UP: do_move_cursor(Step::DisplayLines, -1, shift); return 0;
                case VK_DOWN: do_move_cursor(Step::DisplayLines, 1, shift); return 0;
                case VK_PRIOR: do_move_cursor(Step::Pages, -1, shift); return 0;
                case VK_NEXT: do_move_cursor(Step::Pages, 1, shift); return 0;
                case VK_HOME: do_move_cursor(control ? Step::BufferEnds : Step::ParagraphEnds, -1, shift); return 0;
                case VK_END: do_move_cursor(control ? Step::BufferEnds : Step::ParagraphEnds, 1, shift); return 0;
                case VK_RETURN: insert_newline(); return 0;
                case VK_TAB:
                    if (!control) input_.commit(U"\t");
                    return 0;
                default: break;
            }
            break;
        }
        case WM_CHAR: {
            wchar_t c = static_cast<wchar_t>(wparam);
            if (c >= 0x20 && c != 0x7F) on_char(c);
            return 0;
        }
        case WM_IME_SETCONTEXT:
            // The view draws the composition itself.
            lparam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
            return DefWindowProcW(hwnd_, msg, wparam, lparam);
        case WM_IME_STARTCOMPOSITION:
            return 0;
        case WM_IME_COMPOSITION:
            on_ime_composition(lparam);
            return 0;
        case WM_IME_ENDCOMPOSITION:
            on_ime_end_composition();
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            SetFocus(hwnd_);
            request_commit();
            static DWORD last_time = 0;
            static POINT last_point = {};
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            DWORD now = GetMessageTime();
            if (msg == WM_LBUTTONDBLCLK) {
                click_count_ = 2;
            } else if (click_count_ == 2 && now - last_time <= GetDoubleClickTime() &&
                       std::abs(pt.x - last_point.x) <= GetSystemMetrics(SM_CXDOUBLECLK) &&
                       std::abs(pt.y - last_point.y) <= GetSystemMetrics(SM_CYDOUBLECLK)) {
                click_count_ = 3;
            } else {
                click_count_ = 1;
            }
            last_time = now;
            last_point = pt;
            SetCapture(hwnd_);
            bool is_selection = (wparam & MK_SHIFT) != 0;
            TextIter cursor = get_iter_at_location(dip(pt.x) - padding_, offset_ + dip(pt.y)).second;
            if (is_selection || click_count_ == 1) {
                buffer_.move_cursor(cursor, is_selection);
            } else {
                buffer_.move_mark(anchor_, cursor);
                TextIter start = cursor, end = cursor;
                start.backward_visible_word_start();
                end.forward_visible_word_end();
                buffer_.select_range(end, start);
            }
            place_cursor_onscreen();
            return 0;
        }
        case WM_MOUSEMOVE:
            if ((wparam & MK_LBUTTON) && GetCapture() == hwnd_) {
                POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                TextIter cursor = get_iter_at_location(dip(pt.x) - padding_, offset_ + dip(pt.y)).second;
                if (click_count_ == 1) {
                    buffer_.move_cursor(cursor, true);
                } else {
                    TextIter bound = buffer_.get_iter_at_mark(anchor_);
                    if (cursor < bound) {
                        cursor.backward_visible_word_start();
                        bound.forward_visible_word_end();
                    } else {
                        bound.backward_visible_word_start();
                        cursor.forward_visible_word_end();
                    }
                    buffer_.select_range(cursor, bound);
                }
                place_cursor_onscreen();
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd_) ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            set_scroll(offset_ - delta / 120.0f * static_cast<float>(lines) * line_height_);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si = {sizeof si};
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, SB_VERT, &si);
            float offset = offset_;
            switch (LOWORD(wparam)) {
                case SB_LINEUP: offset -= line_height_; break;
                case SB_LINEDOWN: offset += line_height_; break;
                case SB_PAGEUP: offset -= view_height(); break;
                case SB_PAGEDOWN: offset += view_height(); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: offset = static_cast<float>(si.nTrackPos); break;
                case SB_TOP: offset = 0; break;
                case SB_BOTTOM: offset = height_; break;
                default: break;
            }
            set_scroll(offset);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

}  // namespace furiganapad
