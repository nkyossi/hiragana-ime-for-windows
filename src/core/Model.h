// Hiragana IME for Windows
//
// Copyright (c) 2024-2026 Esrille Inc. (ibus-hiragana)
// Windows port: Copyright (c) 2026 Hiragana IME for Windows contributors
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

// Port of the language-model interface used by engine/llm.py.
//
// The original ibus-hiragana uses a local BERT masked language model
// (cl-tohoku/bert-base-japanese-v3) to pick the conversion candidate that
// best fits the surrounding text.  This header defines the same interface the
// original LanguageModel exposes so that Dictionary::assisted_lookup() can be
// ported unchanged:
//
//   llm.assist(prefix, yomi, words)         -> {index: probability}
//   llm.assist_yougen(prefix, yomi, words)  -> {index: probability}
//
// In the original these return a Python dict whose keys are indexes into
// `words` and whose values are probabilities.  The caller takes the key with
// the greatest value (max(p_dict, key=p_dict.get)); ties are won by the
// smallest index.  std::map<index_t, double> reproduces both the ascending
// iteration order and the tie break.

#include <map>
#include <string>
#include <vector>

#include "Text.h"

namespace hiragana {

class Model {
public:
    virtual ~Model() = default;

    // Returns the probability of each candidate in `words` given the text
    // before the reading (`prefix`).  Keys are indexes into `words`.
    virtual std::map<index_t, double> assist(const ustring& prefix, const ustring& yomi,
                                             const std::vector<ustring>& words) = 0;

    // The same for inflected words (yougen); `words` holds the stems.
    virtual std::map<index_t, double> assist_yougen(const ustring& prefix, const ustring& yomi,
                                                    const std::vector<ustring>& words) = 0;

    // The device the model runs on ("cpu", "gpu", ...), for logging/settings.
    virtual std::string device_type() const = 0;
};

}  // namespace hiragana
