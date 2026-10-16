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

#include <filesystem>
#include <memory>
#include <vector>

#include "Text.h"

namespace hiragana {

// A thin wrapper over MeCab (mecab_sparse_tonode2) used by BertTokenizer: it
// splits text into the surface forms of its morphemes, which are the "words"
// that fugashi's GenericTagger yields for MecabTokenizer.
class MecabTagger {
public:
    // dic_dir is a MeCab dictionary directory (unidic-lite's dicdir).
    explicit MecabTagger(const std::filesystem::path& dic_dir);
    ~MecabTagger();

    MecabTagger(const MecabTagger&) = delete;
    MecabTagger& operator=(const MecabTagger&) = delete;

    std::vector<ustring> surfaces(const ustring& text) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hiragana
