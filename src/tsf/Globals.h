// Hiragana IME for Windows
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

#pragma once

#include <windows.h>

#include <msctf.h>

#include <filesystem>
#include <string>

namespace hiragana::tsf {

extern HINSTANCE g_module;

// {1C841C5A-1337-4E4E-BC44-5B0EA1769018}
inline constexpr CLSID CLSID_TextService = {
    0x1c841c5a, 0x1337, 0x4e4e, {0xbc, 0x44, 0x5b, 0x0e, 0xa1, 0x76, 0x90, 0x18}};
// {C27534CD-9A44-4271-BDDE-E9FC246A1DD5}
inline constexpr GUID GUID_Profile = {
    0xc27534cd, 0x9a44, 0x4271, {0xbd, 0xde, 0xe9, 0xfc, 0x24, 0x6a, 0x1d, 0xd5}};
// {3B1161A8-D37C-4FAD-9F35-4857AEB96EBF}
inline constexpr GUID GUID_DisplayAttributeInput = {
    0x3b1161a8, 0xd37c, 0x4fad, {0x9f, 0x35, 0x48, 0x57, 0xae, 0xb9, 0x6e, 0xbf}};
// {BC2B9638-D8C6-46BC-BED9-E81C4F650904}
inline constexpr GUID GUID_DisplayAttributeCandidate = {
    0xbc2b9638, 0xd8c6, 0x46bc, {0xbe, 0xd9, 0xe8, 0x1c, 0x4f, 0x65, 0x09, 0x04}};
// {25CC0B8E-EA35-4C08-9331-EB17BE798515}
inline constexpr GUID GUID_DisplayAttributeLocked = {
    0x25cc0b8e, 0xea35, 0x4c08, {0x93, 0x31, 0xeb, 0x17, 0xbe, 0x79, 0x85, 0x15}};

inline constexpr LANGID kLangId = MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);
inline constexpr wchar_t kDescription[] = L"ひらがなIME";
inline constexpr wchar_t kAppName[] = L"Hiragana IME";

// Counts the live COM objects so that DllCanUnloadNow() can answer.
void DllAddRef();
void DllRelease();
LONG DllRefCount();

// The installation directory (holds dic/ and layouts/).
std::filesystem::path module_dir();
// %APPDATA%\HiraganaIME
std::filesystem::path user_data_dir();
std::filesystem::path settings_path();
std::wstring module_path();

// Writes the log of the core library to %APPDATA%\HiraganaIME\hiragana-ime.log.
void init_logging();

}  // namespace hiragana::tsf
