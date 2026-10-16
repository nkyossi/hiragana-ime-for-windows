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

// Port of window.py and application.py.

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "FuriganaView.h"

namespace furiganapad {

class App;

class MainWindow {
public:
    MainWindow(App& app, const std::filesystem::path& file);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    HWND hwnd() const { return hwnd_; }
    const std::filesystem::path& file() const { return file_; }
    bool is_empty() { return view_->buffer().empty() && file_.empty(); }
    // Returns false if the user canceled closing.
    bool close();
    void present();
    // Handles an accelerator key; returns true if it was used.
    bool translate_accelerator(MSG& msg);
    // The text of the document as it is saved, for tests.
    FuriganaView& view() { return *view_; }

private:
    enum Bar { kNone, kFind, kReplace, kRuby };

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void create_menu();
    void create_bars();
    void layout();
    void on_command(int id);
    void on_edit_key(HWND edit, UINT key, bool shift);

    void load_file(const std::filesystem::path& file);
    bool save();
    bool save_as();
    // Returns true if closing should be canceled.
    bool confirm_save_changes();
    void open();
    void show_bar(Bar bar);
    void find();
    void replace_dialog();
    void annotate();
    void on_find(HWND entry);
    void on_replace();
    void on_ruby();
    bool select_text(const ustring& text);
    void choose_font();
    void update_title();
    void update_statusbar();
    void update_menu();
    void about();
    void help();
    HWND focused_edit() const;

    App& app_;
    HWND hwnd_ = nullptr;
    std::unique_ptr<FuriganaView> view_;
    HWND bar_label_ = nullptr;
    HWND bar_edit1_ = nullptr;
    HWND bar_edit2_ = nullptr;
    Bar bar_kind_ = kNone;
    HWND status_ = nullptr;
    HFONT ui_font_ = nullptr;
    std::filesystem::path file_;
    bool ruby_mode_ = true;
    std::wstring search_text_;
    std::wstring replace_from_text_;
    std::wstring replace_to_text_;
};

struct AppSettings {
    ViewSettings view;
    bool statusbar = true;
    int width = 720;
    int height = 512;
};

class App {
public:
    App();
    int run(int argc, wchar_t** argv);

    void new_window(const std::filesystem::path& file = std::filesystem::path());
    void window_closed(MainWindow* window);
    void quit();
    MainWindow* is_opened(const std::filesystem::path& file);
    AppSettings& settings() { return settings_; }
    void save_settings();
    HACCEL accelerators() const { return accelerators_; }

private:
    void load_settings();

    std::vector<std::unique_ptr<MainWindow>> windows_;
    std::vector<std::unique_ptr<MainWindow>> closed_;
    AppSettings settings_;
    HACCEL accelerators_ = nullptr;
};

}  // namespace furiganapad
