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

#include "Nfkc.h"

#include <string>

#include <windows.h>

namespace hiragana {

ustring nfkc(const ustring& text) {
    if (text.empty()) return ustring();
    const std::u16string utf16 = to_utf16(text);
    if (utf16.empty()) return text;
    const int source_length = static_cast<int>(utf16.size());
    const auto* source = reinterpret_cast<LPCWSTR>(utf16.c_str());
    const int needed = NormalizeString(NormalizationKC, source, source_length, nullptr, 0);
    if (needed <= 0) return text;  // invalid input; leave the text as it is
    std::u16string buffer(static_cast<size_t>(needed), u'\0');
    const int written = NormalizeString(NormalizationKC, source, source_length,
                                        reinterpret_cast<LPWSTR>(buffer.data()),
                                        static_cast<int>(buffer.size()));
    if (written <= 0) return text;
    buffer.resize(static_cast<size_t>(written));
    return from_utf16(buffer);
}

}  // namespace hiragana
