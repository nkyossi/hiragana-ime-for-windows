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

#include "MainWindow.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

#include "Json.h"
#include "Markdown.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace furiganapad {

namespace {

const wchar_t kClassName[] = L"FuriganaPadWindow";
const wchar_t kTitle[] = L"ふりがなパッド";

enum Command : int {
    ID_NEW = 100,
    ID_OPEN,
    ID_SAVE,
    ID_SAVEAS,
    ID_CLOSE,
    ID_QUIT,
    ID_UNDO,
    ID_REDO,
    ID_CUT,
    ID_COPY,
    ID_PASTE,
    ID_SELECTALL,
    ID_UNSELECT,
    ID_FIND,
    ID_REPLACE,
    ID_ANNOTATE,
    ID_UNCONVERT,
    ID_FONT,
    ID_RUBY,
    ID_HIGHLIGHT,
    ID_MARKDOWN,
    ID_STATUSBAR,
    ID_HELP,
    ID_ABOUT,
};

bool is_markdown_file(const fs::path& path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".md" || ext == L".markdown";
}

fs::path data_dir() {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &path)))
        result = fs::path(path) / L"HiraganaIME";
    CoTaskMemFree(path);
    return result;
}

fs::path exe_dir() {
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return fs::path(std::wstring(buffer, n)).parent_path();
}

}  // namespace

//
// MainWindow
//
MainWindow::MainWindow(App& app, const fs::path& file) : app_(app) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof wc};
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_FURIGANAPAD));
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    const AppSettings& s = app_.settings();
    hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            s.width, s.height, nullptr, nullptr, GetModuleHandleW(nullptr), this);

    NONCLIENTMETRICSW ncm = {sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    ui_font_ = CreateFontIndirectW(&ncm.lfMessageFont);

    FuriganaView::Callbacks callbacks;
    callbacks.cursor_moved = [this] { update_statusbar(); };
    callbacks.modified_changed = [this] { update_title(); };
    view_ = std::make_unique<FuriganaView>(hwnd_, callbacks, s.view);
    view_->buffer().set_ruby_mode(ruby_mode_);

    create_menu();
    create_bars();
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_RIGHT | SS_CENTERIMAGE, 0, 0, 0, 0, hwnd_, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
    if (s.statusbar) ShowWindow(status_, SW_SHOW);

    load_file(file);
    update_menu();
    update_statusbar();
    update_title();
    layout();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetFocus(view_->hwnd());
}

MainWindow::~MainWindow() {
    view_.reset();
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    if (ui_font_) DeleteObject(ui_font_);
}

LRESULT CALLBACK MainWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* window = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        window->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    auto* window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window) return window->handle(msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void MainWindow::create_menu() {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_NEW, L"新しいウィンドウ(&N)\tCtrl+N");
    AppendMenuW(file, MF_STRING, ID_OPEN, L"開く(&O)…\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_SAVE, L"保存(&S)\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_SAVEAS, L"名前をつけて保存(&A)…\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_CLOSE, L"閉じる(&C)\tCtrl+W");
    AppendMenuW(file, MF_STRING, ID_QUIT, L"終了(&Q)\tCtrl+Q");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"ファイル(&F)");

    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, ID_UNDO, L"元に戻す(&U)\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, ID_REDO, L"やり直す(&R)\tCtrl+Shift+Z");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_CUT, L"切り取り(&T)\tCtrl+X");
    AppendMenuW(edit, MF_STRING, ID_COPY, L"コピー(&C)\tCtrl+C");
    AppendMenuW(edit, MF_STRING, ID_PASTE, L"貼り付け(&P)\tCtrl+V");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_SELECTALL, L"すべて選択(&A)\tCtrl+A");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_FIND, L"検索(&F)…\tCtrl+F");
    AppendMenuW(edit, MF_STRING, ID_REPLACE, L"置換(&R)…\tCtrl+H");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_ANNOTATE, L"ルビをふる(&B)…\tCtrl+B");
    AppendMenuW(edit, MF_STRING, ID_UNCONVERT, L"ひらがなに戻す(&H)\tCtrl+T");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"編集(&E)");

    HMENU settings = CreatePopupMenu();
    AppendMenuW(settings, MF_STRING, ID_FONT, L"フォントを変える(&F)…");
    AppendMenuW(settings, MF_STRING, ID_RUBY, L"ルビをふる(&R)\tCtrl+R");
    AppendMenuW(settings, MF_STRING, ID_HIGHLIGHT, L"長い文をめだたせる(&L)");
    AppendMenuW(settings, MF_STRING, ID_MARKDOWN, L"Markdownとして表示する(&M)");
    AppendMenuW(settings, MF_STRING, ID_STATUSBAR, L"ステータスバー(&S)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"設定(&S)");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_HELP, L"ヘルプ(&H)\tF1");
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"ふりがなパッドについて(&A)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"ヘルプ(&H)");
    SetMenu(hwnd_, bar);
}

void MainWindow::create_bars() {
    bar_label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 0, 0, hwnd_,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(bar_label_, SW_HIDE);
    for (HWND* edit : {&bar_edit1_, &bar_edit2_}) {
        *edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(*edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
        SetWindowSubclass(*edit, edit_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    SendMessageW(bar_label_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
    SendMessageW(bar_edit1_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L""));
}

LRESULT CALLBACK MainWindow::edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) {
    auto* window = reinterpret_cast<MainWindow*>(data);
    if (msg == WM_KEYDOWN && (wparam == VK_RETURN || wparam == VK_ESCAPE || wparam == VK_TAB)) {
        window->on_edit_key(hwnd, static_cast<UINT>(wparam), GetKeyState(VK_SHIFT) < 0);
        return 0;
    }
    if (msg == WM_CHAR && (wparam == L'\r' || wparam == 0x1b || wparam == L'\t')) return 0;  // no beep
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, msg, wparam, lparam);
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void MainWindow::layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, ui_font_);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    int row = tm.tmHeight + 12;
    int bottom = rc.bottom;
    if (IsWindowVisible(status_)) {
        bottom -= row;
        MoveWindow(status_, 0, bottom, rc.right - 10, row, TRUE);
    }
    if (bar_kind_ != kNone) {
        int rows = bar_kind_ == kReplace ? 2 : 1;
        int top = bottom - rows * row;
        int label_width = bar_kind_ == kRuby ? tm.tmAveCharWidth * 8 : 0;
        int edit_width = (std::min)(static_cast<int>(rc.right) - 16 - label_width, static_cast<int>(tm.tmAveCharWidth) * 60);
        MoveWindow(bar_label_, 8, top + 4, label_width, row - 8, TRUE);
        MoveWindow(bar_edit1_, 8 + label_width, top + 4, edit_width, row - 8, TRUE);
        MoveWindow(bar_edit2_, 8 + label_width, top + row + 2, edit_width, row - 8, TRUE);
        bottom = top;
    }
    MoveWindow(view_->hwnd(), 0, 0, rc.right, bottom, TRUE);
}

bool MainWindow::translate_accelerator(MSG& msg) {
    return TranslateAcceleratorW(hwnd_, app_.accelerators(), &msg) != 0;
}

HWND MainWindow::focused_edit() const {
    HWND focus = GetFocus();
    return (focus == bar_edit1_ || focus == bar_edit2_) ? focus : nullptr;
}

LRESULT MainWindow::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            if (view_) layout();
            return 0;
        case WM_SETFOCUS:
            if (view_) SetFocus(view_->hwnd());
            return 0;
        case WM_COMMAND:
            if (HIWORD(wparam) == 0 || HIWORD(wparam) == 1) on_command(LOWORD(wparam));
            return 0;
        case WM_INITMENUPOPUP:
            update_menu();
            return 0;
        case WM_CLOSE:
            close();
            return 0;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wparam);
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                wchar_t path[MAX_PATH * 2];
                DragQueryFileW(drop, i, path, static_cast<UINT>(std::size(path)));
                if (MainWindow* w = app_.is_opened(path))
                    w->present();
                else if (i == 0 && view_->buffer().empty() && file_.empty())
                    load_file(path);
                else
                    app_.new_window(path);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_DPICHANGED: {
            auto* rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lparam) == status_ || reinterpret_cast<HWND>(lparam) == bar_label_) {
                SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

void MainWindow::on_command(int id) {
    HWND edit = focused_edit();
    switch (id) {
        case ID_NEW: app_.new_window(); break;
        case ID_OPEN: open(); break;
        case ID_SAVE:
            if (file_.empty())
                save_as();
            else
                save();
            break;
        case ID_SAVEAS: save_as(); break;
        case ID_CLOSE: close(); break;
        case ID_QUIT: app_.quit(); break;
        case ID_UNDO:
            if (edit)
                SendMessageW(edit, EM_UNDO, 0, 0);
            else
                view_->undo();
            break;
        case ID_REDO:
            if (!edit) view_->redo();
            break;
        case ID_CUT:
            if (edit)
                SendMessageW(edit, WM_CUT, 0, 0);
            else
                view_->cut();
            break;
        case ID_COPY:
            if (edit)
                SendMessageW(edit, WM_COPY, 0, 0);
            else
                view_->copy();
            break;
        case ID_PASTE:
            if (edit)
                SendMessageW(edit, WM_PASTE, 0, 0);
            else
                view_->paste();
            break;
        case ID_SELECTALL:
            if (edit)
                SendMessageW(edit, EM_SETSEL, 0, -1);
            else
                view_->select_all(true);
            break;
        case ID_UNSELECT:
            if (!edit) view_->select_all(false);
            break;
        case ID_FIND: find(); break;
        case ID_REPLACE: replace_dialog(); break;
        case ID_ANNOTATE: annotate(); break;
        case ID_UNCONVERT: {
            FuriganaBuffer& b = view_->buffer();
            b.begin_user_action();
            b.unconvert(b.get_cursor());
            b.end_user_action();
            break;
        }
        case ID_FONT: choose_font(); break;
        case ID_RUBY:
            ruby_mode_ = !ruby_mode_;
            view_->buffer().set_ruby_mode(ruby_mode_);
            update_statusbar();
            break;
        case ID_HIGHLIGHT:
            view_->set_check_sentences(!view_->get_check_sentences());
            app_.settings().view.highlight_sentences = view_->get_check_sentences();
            app_.save_settings();
            break;
        case ID_MARKDOWN:
            view_->set_markdown(!view_->markdown());
            update_statusbar();
            break;
        case ID_STATUSBAR:
            ShowWindow(status_, IsWindowVisible(status_) ? SW_HIDE : SW_SHOW);
            app_.settings().statusbar = IsWindowVisible(status_) != FALSE;
            app_.save_settings();
            layout();
            break;
        case ID_HELP: help(); break;
        case ID_ABOUT: about(); break;
        default: break;
    }
    update_menu();
}

void MainWindow::update_menu() {
    HMENU menu = GetMenu(hwnd_);
    CheckMenuItem(menu, ID_RUBY, ruby_mode_ ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(menu, ID_HIGHLIGHT, view_->get_check_sentences() ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(menu, ID_MARKDOWN, view_->markdown() ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(menu, ID_STATUSBAR, IsWindowVisible(status_) ? MF_CHECKED : MF_UNCHECKED);
}

void MainWindow::update_title() {
    std::wstring title = kTitle;
    if (!file_.empty()) title = file_.filename().wstring() + L" – " + title;
    if (view_->buffer().get_modified()) title = L"*" + title;
    SetWindowTextW(hwnd_, title.c_str());
}

void MainWindow::update_statusbar() {
    TextIter cursor = view_->buffer().get_cursor();
    std::wstring status = std::to_wstring(cursor.get_line() + 1) + L":" +
                          std::to_wstring(cursor.get_plain_line_offset() + 1) + L"    ";
    status += ruby_mode_ ? L"ルビ" : L"ルビ（オフ）";
    if (view_->markdown()) status += L"    Markdown";
    SetWindowTextW(status_, status.c_str());
}

void MainWindow::present() {
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
}

//
// Files
//
void MainWindow::load_file(const fs::path& file) {
    file_ = file;
    view_->set_markdown(!file.empty() && is_markdown_file(file));
    if (!file.empty()) {
        std::ifstream f(file, std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            ustring content = from_utf8(ss.str());
            if (is_markdown_file(file)) content = markdown_to_annotated(content);
            content = remove_dangling_annotations(content);
            FuriganaBuffer& b = view_->buffer();
            if (!content.empty()) {
                b.begin_user_action();
                b.set_text(content);
                b.end_user_action();
                b.place_cursor(b.get_start_iter());
            }
            b.set_modified(false);
        } else {
            // Create a new file
            save();
        }
    }
    update_title();
    update_statusbar();
    view_->place_cursor_onscreen();
}

bool MainWindow::save() {
    view_->commit_input();
    FuriganaBuffer& b = view_->buffer();
    auto [start, end] = b.get_bounds();
    ustring contents = b.get_text(start, end, true);
    if (!contents.empty()) contents += U"\n";
    if (is_markdown_file(file_)) contents = annotated_to_markdown(contents);
    std::ofstream f(file_, std::ios::binary | std::ios::trunc);
    std::string bytes = to_utf8(contents);
    if (f) f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    f.close();
    if (!f) {
        std::wstring message = L"ファイルを保存できませんでした。\n" + file_.wstring();
        MessageBoxW(hwnd_, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
        return save_as();
    }
    b.set_modified(false);
    update_title();
    return false;
}

static bool file_dialog(HWND owner, bool save, const fs::path& current, fs::path& result) {
    ComPtr<IFileDialog> dialog;
    HRESULT hr = save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
                      : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return false;
    COMDLG_FILTERSPEC filters[] = {
        {L"テキスト ファイル (*.txt)", L"*.txt"},
        {L"Markdown (*.md)", L"*.md;*.markdown"},
        {L"すべてのファイル", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetTitle(save ? L"名前をつけて保存" : L"ファイルを開く");
    if (save) {
        dialog->SetDefaultExtension(L"txt");
        dialog->SetFileTypeIndex(is_markdown_file(current) ? 2 : 1);
        if (!current.empty()) {
            dialog->SetFileName(current.filename().c_str());
            ComPtr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(current.parent_path().c_str(), nullptr, IID_PPV_ARGS(&folder))))
                dialog->SetFolder(folder.Get());
        }
    }
    if (FAILED(dialog->Show(owner))) return false;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return false;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return false;
    result = path;
    CoTaskMemFree(path);
    return true;
}

bool MainWindow::save_as() {
    fs::path path;
    if (!file_dialog(hwnd_, true, file_, path)) return true;
    file_ = path;
    if (is_markdown_file(file_) && !view_->markdown()) view_->set_markdown(true);
    bool result = save();
    update_title();
    update_statusbar();
    return result;
}

bool MainWindow::confirm_save_changes() {
    view_->commit_input();
    if (!view_->buffer().get_modified()) return false;
    TASKDIALOGCONFIG config = {sizeof config};
    TASKDIALOG_BUTTON buttons[] = {
        {IDYES, L"保存(&S)"},
        {IDNO, L"保存しないで閉じる(&W)"},
        {IDCANCEL, L"キャンセル"},
    };
    config.hwndParent = hwnd_;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    config.pszWindowTitle = kTitle;
    config.pszMainInstruction = L"書きかえた文章を保存しますか？";
    config.pszContent = L"保存しないと、書きかえた文章は残りません。";
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = IDYES;
    int button = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&config, &button, nullptr, nullptr))) {
        button = MessageBoxW(hwnd_, L"書きかえた文章を保存しますか？", kTitle, MB_YESNOCANCEL | MB_ICONQUESTION);
    }
    if (button == IDNO) return false;
    if (button == IDYES) return file_.empty() ? save_as() : save();
    return true;
}

bool MainWindow::close() {
    if (confirm_save_changes()) return false;
    app_.window_closed(this);
    return true;
}

void MainWindow::open() {
    fs::path path;
    if (!file_dialog(hwnd_, false, file_, path)) return;
    if (MainWindow* w = app_.is_opened(path))
        w->present();
    else if (!view_->buffer().empty() || !file_.empty())
        app_.new_window(path);
    else
        load_file(path);
}

//
// Search bars
//
void MainWindow::show_bar(Bar bar) {
    bar_kind_ = bar;
    ShowWindow(bar_label_, bar == kRuby ? SW_SHOW : SW_HIDE);
    ShowWindow(bar_edit1_, bar != kNone ? SW_SHOW : SW_HIDE);
    ShowWindow(bar_edit2_, bar == kReplace ? SW_SHOW : SW_HIDE);
    if (bar == kRuby) SetWindowTextW(bar_label_, L"ルビ: ");
    const wchar_t* cue = bar == kFind ? L"検索" : bar == kReplace ? L"置換するもの" : L"";
    SendMessageW(bar_edit1_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
    SendMessageW(bar_edit2_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"置換後"));
    layout();
    if (bar == kNone) {
        SetFocus(view_->hwnd());
    } else {
        SetFocus(bar_edit1_);
        SendMessageW(bar_edit1_, EM_SETSEL, 0, -1);
    }
}

static std::wstring window_text(HWND hwnd) {
    int n = GetWindowTextLengthW(hwnd);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

void MainWindow::find() {
    FuriganaBuffer& b = view_->buffer();
    auto [start, end] = b.get_selection_bounds();
    if (start != end) {
        ustring text = b.get_text(start, end, false);
        text.erase(std::remove(text.begin(), text.end(), U'\n'), text.end());
        search_text_ = to_wide(text);
    }
    SetWindowTextW(bar_edit1_, search_text_.c_str());
    show_bar(kFind);
}

void MainWindow::replace_dialog() {
    FuriganaBuffer& b = view_->buffer();
    auto [start, end] = b.get_selection_bounds();
    if (start != end) {
        ustring text = b.get_text(start, end, false);
        text.erase(std::remove(text.begin(), text.end(), U'\n'), text.end());
        replace_from_text_ = to_wide(text);
    }
    SetWindowTextW(bar_edit1_, replace_from_text_.c_str());
    SetWindowTextW(bar_edit2_, replace_to_text_.c_str());
    show_bar(kReplace);
}

void MainWindow::annotate() {
    FuriganaBuffer& b = view_->buffer();
    auto [start, end] = b.get_selection_bounds();
    if (start != end && start.get_line() == end.get_line()) {
        SetWindowTextW(bar_edit1_, L"");
        show_bar(kRuby);
    }
}

void MainWindow::on_edit_key(HWND edit, UINT key, bool shift) {
    if (key == VK_ESCAPE) {
        if (bar_kind_ == kFind) search_text_ = window_text(bar_edit1_);
        if (bar_kind_ == kReplace) {
            replace_from_text_ = window_text(bar_edit1_);
            replace_to_text_ = window_text(bar_edit2_);
        }
        show_bar(kNone);
        return;
    }
    if (key == VK_TAB) {
        if (bar_kind_ == kReplace) SetFocus(edit == bar_edit1_ ? bar_edit2_ : bar_edit1_);
        return;
    }
    // Return
    (void)shift;
    switch (bar_kind_) {
        case kFind: on_find(bar_edit1_); break;
        case kReplace:
            if (edit == bar_edit1_)
                on_find(bar_edit1_);
            else
                on_replace();
            break;
        case kRuby: on_ruby(); break;
        default: break;
    }
}

void MainWindow::on_find(HWND entry) {
    select_text(get_plain_text(from_wide(window_text(entry))));
}

void MainWindow::on_replace() {
    FuriganaBuffer& b = view_->buffer();
    ustring text_from = get_plain_text(from_wide(window_text(bar_edit1_)));
    auto [start, end] = b.get_selection_bounds();
    if (start != end) {
        ustring selection = b.get_text(start, end, false);
        if (selection == text_from) {
            ustring text_to = from_wide(window_text(bar_edit2_));
            b.begin_user_action();
            b.delete_(start, end);
            b.insert_at_cursor(text_to);
            b.end_user_action();
            view_->scroll_mark_onscreen(b.get_insert());
        }
    }
    select_text(text_from);
}

void MainWindow::on_ruby() {
    FuriganaBuffer& b = view_->buffer();
    auto [start, end] = b.get_selection_bounds();
    if (start != end && start.get_line() == end.get_line()) {
        ustring text = b.get_text(start, end, false);
        ustring ruby = get_plain_text(from_wide(window_text(bar_edit1_)));
        b.begin_user_action();
        b.delete_(start, end);
        if (!ruby.empty()) text = ustring(1, IAA) + text + ustring(1, IAS) + ruby + ustring(1, IAT);
        b.insert_at_cursor(text);
        b.end_user_action();
    }
    show_bar(kNone);
}

bool MainWindow::select_text(const ustring& text) {
    FuriganaBuffer& b = view_->buffer();
    TextIter start = b.get_iter_at_mark(b.get_insert());
    TextIter selected = b.get_iter_at_mark(b.get_selection_bound());
    if (start < selected) start = selected;
    auto match = start.forward_search(text, nullptr);
    if (!match) {
        start = b.get_start_iter();
        match = start.forward_search(text, nullptr);
    }
    if (match) {
        b.select_range(match->first, match->second);
        view_->scroll_mark_onscreen(b.get_insert());
        return true;
    }
    MessageBeep(MB_OK);
    return false;
}

//
// Settings and help
//
void MainWindow::choose_font() {
    LOGFONTW lf = {};
    const ViewSettings& s = view_->settings();
    wcsncpy_s(lf.lfFaceName, s.font_family.c_str(), _TRUNCATE);
    HDC dc = GetDC(hwnd_);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(hwnd_, dc);
    lf.lfHeight = -MulDiv(static_cast<int>(s.font_size + 0.5f), dpi, 96);
    lf.lfCharSet = DEFAULT_CHARSET;
    CHOOSEFONTW cf = {sizeof cf};
    cf.hwndOwner = hwnd_;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
    if (!ChooseFontW(&cf)) return;
    float size = static_cast<float>(MulDiv(std::abs(lf.lfHeight), 96, dpi));
    view_->set_font(lf.lfFaceName, size);
    app_.settings().view.font_family = view_->settings().font_family;
    app_.settings().view.font_size = view_->settings().font_size;
    app_.save_settings();
}

void MainWindow::help() {
    fs::path index = exe_dir() / L"furiganapad-docs" / L"index.html";
    std::error_code ec;
    if (fs::exists(index, ec))
        ShellExecuteW(hwnd_, L"open", index.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else
        ShellExecuteW(hwnd_, L"open", L"https://esrille.github.io/furiganapad/", nullptr, nullptr, SW_SHOWNORMAL);
}

void MainWindow::about() {
    std::wstring text = L"ふりがなパッド for Windows\nバージョン " FURIGANAPAD_VERSION_W L"\n\n"
                        L"FuriganaPad (Copyright 2019-2025 Esrille Inc.) を Windows に移植したものです。\n"
                        L"GNU Lesser General Public License 2.1 以降\n\n"
                        L"ひらがなIMEといっしょにつかうと、入力した漢字に自動的にふりがなをふります。";
    MessageBoxW(hwnd_, text.c_str(), L"ふりがなパッドについて", MB_OK | MB_ICONINFORMATION);
}

//
// App
//
App::App() {
    load_settings();
    ACCEL accels[] = {
        {FCONTROL | FVIRTKEY, 'N', ID_NEW},
        {FCONTROL | FVIRTKEY, 'O', ID_OPEN},
        {FCONTROL | FVIRTKEY, 'S', ID_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_SAVEAS},
        {FCONTROL | FVIRTKEY, 'W', ID_CLOSE},
        {FCONTROL | FVIRTKEY, 'Q', ID_QUIT},
        {FCONTROL | FVIRTKEY, 'Z', ID_UNDO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'Z', ID_REDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_REDO},
        {FCONTROL | FVIRTKEY, 'X', ID_CUT},
        {FCONTROL | FVIRTKEY, 'C', ID_COPY},
        {FCONTROL | FVIRTKEY, 'V', ID_PASTE},
        {FCONTROL | FVIRTKEY, 'A', ID_SELECTALL},
        {FCONTROL | FVIRTKEY, VK_OEM_5, ID_UNSELECT},
        {FCONTROL | FVIRTKEY, 'F', ID_FIND},
        {FCONTROL | FVIRTKEY, 'H', ID_REPLACE},
        {FCONTROL | FVIRTKEY, 'B', ID_ANNOTATE},
        {FCONTROL | FVIRTKEY, 'T', ID_UNCONVERT},
        {FCONTROL | FVIRTKEY, 'R', ID_RUBY},
        {FVIRTKEY, VK_F1, ID_HELP},
    };
    accelerators_ = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));
}

void App::load_settings() {
    std::ifstream f(data_dir() / L"furiganapad.json", std::ios::binary);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();
    try {
        hiragana::json::Value v = hiragana::json::parse(ss.str());
        if (v["font-family"].is_string()) settings_.view.font_family = to_wide(from_utf8(v["font-family"].as_string()));
        if (v["font-size"].is_number()) settings_.view.font_size = static_cast<float>(v["font-size"].as_number());
        settings_.view.highlight_sentences = v["highlight-long-sentences"].as_bool(true);
        settings_.statusbar = v["statusbar"].as_bool(true);
        if (v["width"].is_number()) settings_.width = static_cast<int>(v["width"].as_number());
        if (v["height"].is_number()) settings_.height = static_cast<int>(v["height"].as_number());
    } catch (...) {
    }
    settings_.view.font_size = std::clamp(settings_.view.font_size, 6.0f, 144.0f);
}

void App::save_settings() {
    hiragana::json::Value v(hiragana::json::Value::Object{});
    v.set("font-family", to_utf8(from_wide(settings_.view.font_family)));
    v.set("font-size", static_cast<double>(settings_.view.font_size));
    v.set("highlight-long-sentences", settings_.view.highlight_sentences);
    v.set("statusbar", settings_.statusbar);
    v.set("width", settings_.width);
    v.set("height", settings_.height);
    std::error_code ec;
    fs::create_directories(data_dir(), ec);
    std::ofstream f(data_dir() / L"furiganapad.json", std::ios::binary | std::ios::trunc);
    f << v.dump();
}

void App::new_window(const fs::path& file) { windows_.push_back(std::make_unique<MainWindow>(*this, file)); }

MainWindow* App::is_opened(const fs::path& file) {
    std::error_code ec;
    for (auto& w : windows_) {
        if (!w->file().empty() && fs::equivalent(w->file(), file, ec)) return w.get();
    }
    return nullptr;
}

void App::window_closed(MainWindow* window) {
    RECT rc;
    if (GetWindowRect(window->hwnd(), &rc) && !IsIconic(window->hwnd()) && !IsZoomed(window->hwnd())) {
        settings_.width = rc.right - rc.left;
        settings_.height = rc.bottom - rc.top;
        save_settings();
    }
    auto it = std::find_if(windows_.begin(), windows_.end(), [&](auto& w) { return w.get() == window; });
    if (it != windows_.end()) {
        ShowWindow(window->hwnd(), SW_HIDE);
        // Destroyed after the message that closed it has been handled.
        closed_.push_back(std::move(*it));
        windows_.erase(it);
    }
    if (windows_.empty()) PostQuitMessage(0);
}

void App::quit() {
    // Close the windows one by one; stop if the user cancels.
    while (!windows_.empty()) {
        MainWindow* w = windows_.back().get();
        w->present();
        if (!w->close()) return;
    }
}

int App::run(int argc, wchar_t** argv) {
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) files.emplace_back(argv[i]);
    if (files.empty()) {
        new_window();
    } else {
        for (const auto& f : files) {
            if (!is_opened(f)) new_window(fs::absolute(f));
        }
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        closed_.clear();
        bool done = false;
        for (auto& w : windows_) {
            if (msg.hwnd == w->hwnd() || IsChild(w->hwnd(), msg.hwnd)) {
                done = w->translate_accelerator(msg);
                break;
            }
        }
        if (done) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    closed_.clear();
    windows_.clear();
    return static_cast<int>(msg.wParam);
}

}  // namespace furiganapad
