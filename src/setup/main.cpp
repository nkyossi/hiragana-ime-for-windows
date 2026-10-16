// Hiragana IME for Windows - setup program
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

// The settings dialog, after setup/main.py of ibus-hiragana. It writes
// %APPDATA%\HiraganaIME\settings.json, which the text service reloads.

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "Settings.h"
#include "Text.h"
#include "resource.h"

namespace fs = std::filesystem;
using hiragana::Settings;

namespace {

const std::pair<const wchar_t*, const char*> kLayouts[] = {
    {L"ローマ字", "roomazi"},
    {L"かな (JIS配列)", "jis"},
    {L"かな (ニュースティックニー配列)", "new_stickney"},
};

const std::pair<const wchar_t*, const char*> kKeyboards[] = {
    {L"自動 (Windowsの設定にあわせる)", "auto"},
    {L"日本語キーボード (106/109)", "jp"},
    {L"英語キーボード (101/104)", "us"},
    {L"英語キーボード (Dvorak)", "us+dvorak"},
};

const std::pair<const wchar_t*, const char*> kDictionaries[] = {
    {L"小学校１年生", "restrained.1.dic"}, {L"小学校２年生", "restrained.2.dic"},
    {L"小学校３年生", "restrained.3.dic"}, {L"小学校４年生", "restrained.4.dic"},
    {L"小学校５年生", "restrained.5.dic"}, {L"小学校６年生", "restrained.6.dic"},
    {L"中学生", "restrained.7.dic"},       {L"高校生", "restrained.8.dic"},
    {L"おとな", "restrained.9.dic"},
};

const std::pair<const wchar_t*, const char*> kModes[] = {
    {L"英数 (A)", "A"},
    {L"ひらがな (あ)", u8"あ"},
};

const std::pair<const wchar_t*, const char*> kLoggingLevels[] = {
    {L"警告だけ (WARNING)", "WARNING"},
    {L"情報 (INFO)", "INFO"},
    {L"デバッグ (DEBUG)", "DEBUG"},
};

fs::path user_data_dir() {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &path)))
        result = fs::path(path) / L"HiraganaIME";
    CoTaskMemFree(path);
    return result;
}

fs::path settings_path() { return user_data_dir() / L"settings.json"; }

std::wstring widen(const std::string& s) { return hiragana::to_wide(hiragana::from_utf8(s)); }
std::string narrow(const std::wstring& s) { return hiragana::to_utf8(hiragana::from_wide(s)); }

// Lets the text service read the settings and dictionaries from apps that
// run in an AppContainer (Store apps, the Start menu search box).
void grant_app_containers_read(const fs::path& dir) {
    const wchar_t* sids[] = {L"S-1-15-2-1", L"S-1-15-2-2"};  // ALL (RESTRICTED) APPLICATION PACKAGES
    PACL old_acl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(dir.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_acl,
                              nullptr, &sd) != ERROR_SUCCESS)
        return;
    std::vector<EXPLICIT_ACCESSW> entries;
    std::vector<PSID> owned;
    for (const wchar_t* text : sids) {
        PSID sid = nullptr;
        if (!ConvertStringSidToSidW(text, &sid)) continue;
        owned.push_back(sid);
        EXPLICIT_ACCESSW ea = {};
        ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        ea.grfAccessMode = GRANT_ACCESS;
        ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        entries.push_back(ea);
    }
    PACL new_acl = nullptr;
    if (!entries.empty() && SetEntriesInAclW(static_cast<ULONG>(entries.size()), entries.data(), old_acl,
                                             &new_acl) == ERROR_SUCCESS) {
        SetNamedSecurityInfoW(const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                              nullptr, new_acl, nullptr);
        LocalFree(new_acl);
    }
    for (PSID sid : owned) LocalFree(sid);
    LocalFree(sd);
}

fs::path local_data_dir() {
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path)))
        result = fs::path(path) / L"HiraganaIME";
    CoTaskMemFree(path);
    return result;
}

fs::path exe_dir() {
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return fs::path(std::wstring(buffer, n)).parent_path();
}

// A file installed next to this program; in a build tree it is next to the
// text service.
fs::path find_installed(const wchar_t* name) {
    std::error_code ec;
    for (const fs::path& dir : {exe_dir(), exe_dir().parent_path() / L"tsf"}) {
        if (fs::exists(dir / name, ec)) return dir / name;
    }
    return fs::path();
}

// Whether the files of the LLM-assisted conversion are there; see hiragana-llm.exe.
bool llm_files_ready() {
    std::error_code ec;
    for (const fs::path& dir : {local_data_dir() / L"llm", exe_dir() / L"llm"}) {
        if (fs::exists(dir / L"model.onnx", ec) && fs::exists(dir / L"vocab.txt", ec) &&
            fs::exists(dir / L"unidic" / L"sys.dic", ec))
            return true;
    }
    return false;
}

fs::path user_dictionary_path(const std::string& name) {
    fs::path p = fs::u8path(name.empty() ? "my.dic" : name);
    return p.is_absolute() ? p : user_data_dir() / p;
}

class SetupDialog {
public:
    static INT_PTR CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<SetupDialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
        if (msg == WM_INITDIALOG) {
            self = reinterpret_cast<SetupDialog*>(lparam);
            SetWindowLongPtrW(hwnd, DWLP_USER, lparam);
            self->hwnd_ = hwnd;
        }
        return self ? self->handle(msg, wparam, lparam) : FALSE;
    }

private:
    INT_PTR handle(UINT msg, WPARAM wparam, LPARAM) {
        switch (msg) {
            case WM_INITDIALOG: {
                HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SETUP));
                SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
                SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
                settings_ = Settings::load(settings_path());
                load_controls();
                return TRUE;
            }
            case WM_COMMAND:
                switch (LOWORD(wparam)) {
                    case IDOK:
                        if (save()) EndDialog(hwnd_, IDOK);
                        return TRUE;
                    case IDCANCEL:
                        EndDialog(hwnd_, IDCANCEL);
                        return TRUE;
                    case IDC_APPLY:
                        save();
                        return TRUE;
                    case IDC_EDIT_USER_DICTIONARY:
                        edit_user_dictionary();
                        return TRUE;
                    case IDC_CLEAR_HISTORY:
                        clear_history();
                        return TRUE;
                    case IDC_GET_LLM:
                        get_llm();
                        return TRUE;
                    case IDC_USE_LLM:
                        update_llm_controls();
                        return TRUE;
                }
                break;
            case WM_ACTIVATE:
                // get-llm.ps1 may have finished meanwhile.
                if (LOWORD(wparam) != WA_INACTIVE) update_llm_controls();
                break;
        }
        return FALSE;
    }

    template <size_t N>
    void fill(int id, const std::pair<const wchar_t*, const char*> (&items)[N], const std::string& current) {
        HWND combo = GetDlgItem(hwnd_, id);
        int selected = 0;
        for (size_t i = 0; i < N; ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i].first));
            if (current == items[i].second) selected = static_cast<int>(i);
        }
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
    }

    template <size_t N>
    std::string selected(int id, const std::pair<const wchar_t*, const char*> (&items)[N]) {
        LRESULT i = SendDlgItemMessageW(hwnd_, id, CB_GETCURSEL, 0, 0);
        return (0 <= i && static_cast<size_t>(i) < N) ? items[i].second : items[0].second;
    }

    void check(int id, bool value) { CheckDlgButton(hwnd_, id, value ? BST_CHECKED : BST_UNCHECKED); }
    bool checked(int id) { return IsDlgButtonChecked(hwnd_, id) == BST_CHECKED; }

    void load_controls() {
        fill(IDC_LAYOUT, kLayouts, settings_.layout);
        fill(IDC_KEYBOARD, kKeyboards, settings_.keyboard);
        fill(IDC_DICTIONARY, kDictionaries, settings_.dictionary);
        fill(IDC_MODE, kModes, settings_.mode);
        fill(IDC_LOGGING, kLoggingLevels, settings_.logging_level);
        check(IDC_NN, settings_.nn_as_jis_x_4063);
        check(IDC_ALTGR, settings_.altgr == "altgr");
        check(IDC_PERMISSIBLE, settings_.permissible);
        check(IDC_HALF_WIDTH_DIGITS, settings_.use_half_width_digits);
        check(IDC_CIRCUMFLEX, settings_.combining_circumflex);
        check(IDC_MACRON, settings_.combining_macron);
        check(IDC_USE_LLM, settings_.use_llm);
        check(IDC_USE_CUDA, settings_.use_cuda);
        update_llm_controls();
        SetDlgItemTextW(hwnd_, IDC_USER_DICTIONARY, widen(settings_.user_dictionary).c_str());
    }

    bool save() {
        // Keep values that may have been changed meanwhile, e.g. history-generation.
        Settings s = Settings::load(settings_path());
        s.layout = selected(IDC_LAYOUT, kLayouts);
        s.keyboard = selected(IDC_KEYBOARD, kKeyboards);
        s.dictionary = selected(IDC_DICTIONARY, kDictionaries);
        s.mode = selected(IDC_MODE, kModes);
        s.logging_level = selected(IDC_LOGGING, kLoggingLevels);
        s.nn_as_jis_x_4063 = checked(IDC_NN);
        s.altgr = checked(IDC_ALTGR) ? "altgr" : "none";
        s.permissible = checked(IDC_PERMISSIBLE);
        s.use_half_width_digits = checked(IDC_HALF_WIDTH_DIGITS);
        s.combining_circumflex = checked(IDC_CIRCUMFLEX);
        s.combining_macron = checked(IDC_MACRON);
        s.use_llm = checked(IDC_USE_LLM);
        s.use_cuda = checked(IDC_USE_CUDA);
        if (s.use_llm && !settings_.use_llm && !llm_files_ready()) {
            if (MessageBoxW(hwnd_,
                            L"LLMを利用するためのファイルがまだありません。\n"
                            L"いま入手しますか？ (約700MBの入手が必要です)",
                            L"ひらがなIMEの設定", MB_YESNO | MB_ICONQUESTION) == IDYES)
                get_llm();
        }
        wchar_t name[MAX_PATH] = {};
        GetDlgItemTextW(hwnd_, IDC_USER_DICTIONARY, name, MAX_PATH);
        std::string user = narrow(name);
        if (user.find("..") != std::string::npos) {
            MessageBoxW(hwnd_, L"個人用辞書名に「..」はつかえません。", L"ひらがなIMEの設定", MB_OK | MB_ICONWARNING);
            return false;
        }
        s.user_dictionary = user;
        if (!s.save(settings_path())) {
            MessageBoxW(hwnd_, L"設定を保存できませんでした。", L"ひらがなIMEの設定", MB_OK | MB_ICONERROR);
            return false;
        }
        grant_app_containers_read(user_data_dir());
        settings_ = s;
        return true;
    }

    void update_llm_controls() {
        bool available = !find_installed(L"hiragana-llm.exe").empty();
        EnableWindow(GetDlgItem(hwnd_, IDC_USE_LLM), available);
        EnableWindow(GetDlgItem(hwnd_, IDC_USE_CUDA), available && checked(IDC_USE_LLM));
        EnableWindow(GetDlgItem(hwnd_, IDC_GET_LLM), available && !find_installed(L"get-llm.ps1").empty());
        const wchar_t* status = !available        ? L"この版ではLLMを利用できません。"
                                : llm_files_ready() ? L"必要なファイルは入手ずみです。"
                                                    : L"必要なファイル (約700MB) がありません。";
        SetDlgItemTextW(hwnd_, IDC_LLM_STATUS, status);
    }

    // Runs get-llm.ps1 in a console window; it needs no administrator rights.
    void get_llm() {
        fs::path script = find_installed(L"get-llm.ps1");
        if (script.empty()) return;
        std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
        ShellExecuteW(hwnd_, L"open", L"powershell.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void edit_user_dictionary() {
        wchar_t name[MAX_PATH] = {};
        GetDlgItemTextW(hwnd_, IDC_USER_DICTIONARY, name, MAX_PATH);
        if (!name[0]) {
            wcscpy_s(name, L"my.dic");
            SetDlgItemTextW(hwnd_, IDC_USER_DICTIONARY, name);
        }
        fs::path path = user_dictionary_path(narrow(name));
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            f << hiragana::user_dictionary_template();
        }
        std::wstring args = L"\"" + path.wstring() + L"\"";
        ShellExecuteW(hwnd_, L"open", L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void clear_history() {
        if (MessageBoxW(hwnd_, L"入力履歴を消去しますか？", L"入力履歴のクリア",
                        MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
            return;
        // Increment history-generation first so that running text services
        // reload without saving their old history.
        Settings s = Settings::load(settings_path());
        s.history_generation += 1;
        s.save(settings_path());
        fs::path dir = user_data_dir() / L"dic";
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == L".dic") {
                std::ofstream f(entry.path(), std::ios::binary | std::ios::trunc);
                f << "; v1.0.0\n";
            } else if (entry.path().extension() == L".bak" || entry.path().extension() == L".tmp") {
                fs::remove(entry.path(), ec);
            }
        }
        settings_.history_generation = s.history_generation;
        MessageBoxW(hwnd_, L"入力履歴を消去しました。", L"入力履歴のクリア", MB_OK | MB_ICONINFORMATION);
    }

    HWND hwnd_ = nullptr;
    Settings settings_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Only one setup window at a time.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\HiraganaIMESetup");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"#32770", L"ひらがなIMEの設定");
        if (existing) SetForegroundWindow(existing);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::error_code ec;
    fs::create_directories(user_data_dir(), ec);
    grant_app_containers_read(user_data_dir());
    SetupDialog dialog;
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETUP), nullptr, SetupDialog::proc,
                    reinterpret_cast<LPARAM>(&dialog));
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
