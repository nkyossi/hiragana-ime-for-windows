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

namespace hiragana::tsf {

// The display attributes of the preedit text, after _update_preedit() in engine.py:
// the text being typed is underlined, and the candidate has a light blue background.
enum class Attribute { Input, Candidate, Locked, Count };

const GUID& attribute_guid(Attribute a);

// Returns S_OK and an ITfDisplayAttributeInfo for guid.
HRESULT get_display_attribute_info(REFGUID guid, ITfDisplayAttributeInfo** info);
HRESULT enum_display_attribute_info(IEnumTfDisplayAttributeInfo** e);

}  // namespace hiragana::tsf
