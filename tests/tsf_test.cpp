// End-to-end test of the installed text service.
//
// Opens a window with a RichEdit control (a TSF document) or a classic EDIT
// control (an IMM32 document through CUAS), activates Hiragana IME for this
// process only, types with SendInput, and checks the text.
//
//   tsf_test [richedit|edit]
//
// Do not touch the keyboard or the mouse while it runs.

#include <windows.h>

#include <msctf.h>
#include <richedit.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr CLSID CLSID_TextService = {0x1c841c5a, 0x1337, 0x4e4e, {0xbc, 0x44, 0x5b, 0x0e, 0xa1, 0x76, 0x90, 0x18}};
constexpr GUID GUID_Profile = {0xc27534cd, 0x9a44, 0x4271, {0xbd, 0xde, 0xe9, 0xfc, 0x24, 0x6a, 0x1d, 0xd5}};

HWND g_main = nullptr;
HWND g_edit = nullptr;
int g_failures = 0;
bool g_richedit = true;
// tools/run-e2e.ps1 -Llm: the language model picks the candidates. At the
// beginning of a text it prefers 強 to 今日 for きょう, as the original does.
const bool g_llm = [] {
    char buffer[8];
    return GetEnvironmentVariableA("HIRAGANA_E2E_LLM", buffer, sizeof buffer) == 1 && buffer[0] == '1';
}();
const wchar_t* kyou() { return g_llm ? L"強" : L"今日"; }
// tools/run-e2e.ps1 -SwapKeys: roomazi.jp, where 変換 converts and the space
// bar types "　", with the key-swaps setting space <-> Henkan.
const bool g_swap = [] {
    char buffer[8];
    return GetEnvironmentVariableA("HIRAGANA_E2E_SWAP", buffer, sizeof buffer) == 1 && buffer[0] == '1';
}();

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

void send_key(WORD vk, bool up, bool extended = false, WORD scan = 0) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = scan ? scan : static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof in);
    Sleep(25);
}

void tap(WORD vk, bool extended = false, WORD scan = 0) {
    send_key(vk, false, extended, scan);
    send_key(vk, true, extended, scan);
}

void shifted(WORD vk) {
    send_key(VK_LSHIFT, false, false, 0x2A);
    tap(vk);
    send_key(VK_LSHIFT, true, false, 0x2A);
}

// Types lowercase letters, digits, '.', ',', '-' and spaces.
void type(const char* s) {
    for (; *s; ++s) {
        char c = *s;
        if ('a' <= c && c <= 'z') tap(static_cast<WORD>(c - 'a' + 'A'));
        else if ('0' <= c && c <= '9') tap(static_cast<WORD>(c));
        else if (c == ' ') tap(VK_SPACE);
        else if (c == '.') tap(VK_OEM_PERIOD);
        else if (c == ',') tap(VK_OEM_COMMA);
        else if (c == '-') tap(VK_OEM_MINUS);
    }
    Sleep(100);
}

std::wstring text() {
    wchar_t buffer[1024] = {};
    SendMessageW(g_edit, WM_GETTEXT, static_cast<WPARAM>(std::size(buffer)), reinterpret_cast<LPARAM>(buffer));
    std::wstring s(buffer);
    // Both RichEdit and the classic EDIT return CR LF for a new line.
    return s;
}

void clear() {
    // Commit whatever is left, then empty the control.
    tap(VK_RETURN);
    Sleep(150);
    SendMessageW(g_edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L""));
    Sleep(150);
}

void expect(const char* name, const std::wstring& expected) {
    Sleep(200);
    std::wstring actual = text();
    bool ok = actual == expected;
    if (!ok) ++g_failures;
    std::printf("%s %-40s expected [%s] actual [%s]\n", ok ? "PASS" : "FAIL", name, utf8(expected).c_str(),
                utf8(actual).c_str());
    std::fflush(stdout);
}

// The text the control shows; in the legacy mode (EDIT) uncommitted text is
// still in the composition, which WM_GETTEXT does not return.
void expect2(const char* name, const std::wstring& modeless, const std::wstring& legacy) {
    expect(name, g_richedit ? modeless : legacy);
}

void run_tests(bool richedit) {
    g_richedit = richedit;
    // Share the key state with the window's thread so that GetKeyState() works here.
    AttachThreadInput(GetCurrentThreadId(), GetWindowThreadProcessId(g_main, nullptr), TRUE);
    Sleep(1500);
    SetForegroundWindow(g_main);
    SetFocus(g_edit);
    Sleep(500);

    // Start with Caps Lock off.
    bool caps_before = (GetKeyState(VK_CAPITAL) & 1) != 0;
    if (caps_before) {
        tap(VK_CAPITAL);
        Sleep(300);
    }
    // Hiragana mode: roomazi.us uses Caps Lock as the IME switch; other
    // layouts use 半角/全角. Try Caps Lock first.
    bool used_caps = false;

    clear();
    type("a");
    Sleep(200);
    if (text() == L"a") {
        clear();
        tap(VK_CAPITAL);
        used_caps = true;
        Sleep(300);
        type("a");
        if (text() != L"あ") {
            // Not toggled by Caps Lock; use 半角/全角.
            tap(VK_CAPITAL);
            used_caps = false;
            clear();
            tap(VK_KANJI);
            Sleep(300);
            type("a");
        }
    }
    expect2("mode: hiragana", L"あ", L"");
    clear();

    if (g_swap) {
        type("kanji");
        tap(VK_SPACE);  // as 変換
        tap(VK_RETURN);
        expect("swap: the space bar converts", L"漢字");
        clear();
        tap(VK_CONVERT, false, 0x79);  // as the space bar
        expect("swap: henkan types a space", L"　");
        clear();
        // The keys keep their own roles in the alphanumeric mode.
        tap(used_caps ? VK_CAPITAL : VK_KANJI);
        Sleep(300);
        type("a");
        tap(VK_SPACE);
        type("b");
        expect("swap: alphanumeric mode keeps the space bar", L"a b");
        clear();
        if (((GetKeyState(VK_CAPITAL) & 1) != 0) != caps_before) tap(VK_CAPITAL);
        std::printf("%s: %d failure(s)\n", richedit ? "richedit" : "edit", g_failures);
        std::fflush(stdout);
        PostMessageW(g_main, WM_CLOSE, 0, 0);
        return;
    }

    type("kanji");
    expect2("romaji", L"かんじ", L"");                // かんじ
    tap(VK_SPACE);
    expect2("convert (composition)", L"漢字", L"");        // 漢字
    tap(VK_RETURN);
    expect("confirm", L"漢字");
    clear();

    type("kanji");
    tap(VK_SPACE);
    tap(VK_ESCAPE);
    if (!g_richedit) tap(VK_RETURN);
    expect("escape reverts to reading", L"かんじ");
    clear();

    type("kanji");
    tap(VK_SPACE);
    tap(VK_DOWN, true);
    tap(VK_RETURN);
    expect("second candidate", L"幹事");                   // 幹事
    clear();
    type("kanji");
    tap(VK_SPACE);
    expect2("learned order", L"幹事", L"");
    tap(VK_DOWN, true);
    tap(VK_RETURN);                                                // back to 漢字 first
    expect("learned order (confirmed)", L"漢字");
    clear();

    type("ugo");
    send_key(VK_LSHIFT, false, false, 0x2A);
    tap(VK_SPACE);
    send_key(VK_LSHIFT, true, false, 0x2A);
    type("ku");
    // The okurigana completes the conversion.
    expect2("okurigana (Shift+Space)", L"動く", L"動"); // 動く
    clear();

    type("kana");
    // Each tap of right Shift turns one more character into katakana.
    tap(VK_RSHIFT, false, 0x36);
    tap(VK_RSHIFT, false, 0x36);
    expect2("katakana by right Shift", L"カナ", L"");      // カナ
    tap(VK_RETURN);
    expect("katakana confirmed", L"カナ");
    clear();

    type("kanji");
    tap(VK_BACK);
    if (!g_richedit) tap(VK_RETURN);
    expect("backspace", L"かん");                          // かん
    // Keys the IME does not use must reach the application.
    tap(VK_BACK);
    expect("backspace passed to the application", L"か");
    tap(VK_RETURN);
    expect("enter passed to the application", L"か\r\n");
    clear();

    type("kyou");
    tap(VK_SPACE);
    tap(VK_RETURN);
    type("ha");
    type("kanji");
    tap(VK_SPACE);
    tap(VK_RETURN);
    expect("text before the reading stays", std::wstring(kyou()) + L"は漢字");  // 今日は漢字
    clear();

    type(".");
    tap(VK_SPACE);
    expect2("full-width space", L"。　", L"");                   // 。＋全角スペース
    clear();

    // Back to the alphanumeric mode.
    if (used_caps)
        tap(VK_CAPITAL);
    else
        tap(VK_KANJI);
    Sleep(300);
    type("abc");
    expect("alphanumeric mode", L"abc");
    clear();

    tap(VK_KANJI);
    Sleep(300);
    type("a");
    expect2("hankaku/zenkaku key", L"あ", L"");
    clear();
    tap(VK_KANJI);
    Sleep(300);

    if (((GetKeyState(VK_CAPITAL) & 1) != 0) != caps_before) tap(VK_CAPITAL);

    std::printf("%s: %d failure(s)\n", richedit ? "richedit" : "edit", g_failures);
    std::fflush(stdout);
    PostMessageW(g_main, WM_CLOSE, 0, 0);
}

// FuriganaPad: the view keeps the preedit out of the text, and the converted
// words get their reading as ruby.
std::wstring ruby(const std::wstring& base, const std::wstring& reading) {
    return L"￹" + base + L"￺" + reading + L"￻";
}

void clear_furiganapad() {
    tap(VK_RETURN);
    Sleep(150);
    send_key(VK_CONTROL, false);
    tap('A');
    send_key(VK_CONTROL, true);
    tap(VK_DELETE, true);
    Sleep(200);
}

void ctrl(WORD vk) {
    send_key(VK_CONTROL, false);
    tap(vk);
    send_key(VK_CONTROL, true);
    Sleep(200);
}

void run_furiganapad_tests() {
    Sleep(2500);
    SetForegroundWindow(g_main);
    Sleep(500);
    // Start with Caps Lock off: the US romaji layout uses it as the IME switch
    // and FuriganaPad starts in Hiragana mode while it is on.
    bool caps_before = (GetKeyState(VK_CAPITAL) & 1) != 0;
    if (caps_before) {
        tap(VK_CAPITAL);
        Sleep(300);
    }

    // Hiragana mode by Caps Lock (roomazi.us) or 半角/全角
    type("a");
    bool used_caps = false;
    if (text() == L"a") {
        clear_furiganapad();
        tap(VK_CAPITAL);
        used_caps = true;
        Sleep(300);
        type("a");
        if (text() != L"あ") {
            tap(VK_CAPITAL);
            used_caps = false;
            clear_furiganapad();
            tap(VK_KANJI);
            Sleep(300);
            type("a");
        }
    }
    expect("furiganapad: hiragana", L"あ");
    clear_furiganapad();

    type("kanji");
    expect("furiganapad: romaji", L"かんじ");
    tap(VK_SPACE);
    expect("furiganapad: reading deleted while converting", L"");
    tap(VK_RETURN);
    expect("furiganapad: furigana", ruby(L"漢字", L"かんじ"));
    clear_furiganapad();

    type("kyou");
    tap(VK_SPACE);
    tap(VK_RETURN);
    type("ha");
    type("kanji");
    tap(VK_SPACE);
    type("desu");
    expect("furiganapad: sentence",
           ruby(kyou(), L"きょう") + L"は" + ruby(L"漢字", L"かんじ") +
               L"です");
    clear_furiganapad();

    type("ugo");
    send_key(VK_LSHIFT, false, false, 0x2A);
    tap(VK_SPACE);
    send_key(VK_LSHIFT, true, false, 0x2A);
    type("ku");
    expect("furiganapad: okurigana", ruby(L"動", L"うご") + L"く");
    clear_furiganapad();

    type("kana");
    tap(VK_RSHIFT, false, 0x36);
    tap(VK_RSHIFT, false, 0x36);
    tap(VK_RETURN);
    expect("furiganapad: katakana", L"カナ");
    clear_furiganapad();

    type("kanji");
    tap(VK_SPACE);
    tap(VK_ESCAPE);
    expect("furiganapad: escape", L"かんじ");
    tap(VK_SPACE);
    tap(VK_RETURN);
    ctrl('Z');
    expect("furiganapad: undo the conversion", L"かんじ");
    // As in the original FuriganaPad, undo leaves the caret at the start of the
    // line, so move it to the end before the keys that the IME passes on.
    tap(VK_END, true);
    tap(VK_BACK);
    expect("furiganapad: backspace passed to the editor", L"かん");
    tap(VK_RETURN);
    expect("furiganapad: enter passed to the editor", L"かん\n");
    clear_furiganapad();

    if (used_caps)
        tap(VK_CAPITAL);
    else
        tap(VK_KANJI);
    Sleep(300);
    type("abc");
    expect("furiganapad: alphanumeric", L"abc");
    clear_furiganapad();

    tap(VK_KANJI);
    Sleep(300);
    type("a");
    expect("furiganapad: hankaku/zenkaku key", L"あ");
    clear_furiganapad();
    tap(VK_KANJI);
    Sleep(300);

    // Put Caps Lock back the way it was.
    if (((GetKeyState(VK_CAPITAL) & 1) != 0) != caps_before) {
        tap(VK_CAPITAL);
        Sleep(200);
    }

    std::printf("furiganapad: %d failure(s)\n", g_failures);
    std::fflush(stdout);
    PostMessageW(g_main, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            if (g_edit) MoveWindow(g_edit, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
            return 0;
        case WM_SETFOCUS:
            if (g_edit) SetFocus(g_edit);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "furiganapad") {
        // tsf_test furiganapad <furiganapad.exe>
        std::string exe = argv[2];
        std::wstring cmd = L"\"" + std::wstring(exe.begin(), exe.end()) + L"\" --activate-hiragana-ime";
        STARTUPINFOW si = {sizeof si};
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            std::printf("cannot start %s\n", exe.c_str());
            return 2;
        }
        WaitForInputIdle(pi.hProcess, 5000);
        Sleep(1000);
        struct Find {
            DWORD pid;
            HWND hwnd;
        } find = {pi.dwProcessId, nullptr};
        EnumWindows(
            [](HWND h, LPARAM lp) -> BOOL {
                auto* f = reinterpret_cast<Find*>(lp);
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                if (pid == f->pid && IsWindowVisible(h)) {
                    f->hwnd = h;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&find));
        if (!find.hwnd) {
            std::printf("no window\n");
            return 2;
        }
        g_main = find.hwnd;
        g_edit = FindWindowExW(g_main, nullptr, L"FuriganaView", nullptr);
        run_furiganapad_tests();
        // Close without saving
        WaitForSingleObject(pi.hProcess, 3000);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return g_failures == 0 ? 0 : 1;
    }
    bool richedit = !(argc > 1 && std::string(argv[1]) == "edit");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Activate Hiragana IME for this process only.
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&profiles)))) {
        std::printf("cannot create ITfInputProcessorProfileMgr\n");
        return 2;
    }

    LoadLibraryW(L"msftedit.dll");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"HiraganaTsfTest";
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    RegisterClassW(&wc);
    g_main = CreateWindowExW(0, wc.lpszClassName, L"Hiragana IME test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200,
                             600, 200, nullptr, nullptr, wc.hInstance, nullptr);
    g_edit = CreateWindowExW(0, richedit ? MSFTEDIT_CLASS : L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 600, 200, g_main, nullptr,
                             wc.hInstance, nullptr);
    if (richedit) {
        // Use TSF in the RichEdit control.
        SendMessageW(g_edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
        SendMessageW(g_edit, EM_SETLANGOPTIONS, 0, 0);
    }
    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Yu Gothic UI");
    SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetForegroundWindow(g_main);
    SetFocus(g_edit);

    HRESULT hr = profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN),
                                           CLSID_TextService, GUID_Profile, nullptr,
                                           TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    std::printf("ActivateProfile: %#lx\n", static_cast<unsigned long>(hr));
    TF_INPUTPROCESSORPROFILE active = {};
    if (SUCCEEDED(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active)))
        std::printf("active profile is ours: %d\n", active.clsid == CLSID_TextService);
    std::fflush(stdout);

    std::thread worker([richedit] { run_tests(richedit); });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    worker.join();
    CoUninitialize();
    return g_failures == 0 ? 0 : 1;
}
