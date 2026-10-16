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

#include "MecabTagger.h"

#include <stdexcept>
#include <string>
#include <string_view>

#include <mecab.h>

namespace hiragana {

struct MecabTagger::Impl {
    mecab_t* tagger = nullptr;
    ~Impl() {
        if (tagger) mecab_destroy(tagger);
    }
};

MecabTagger::MecabTagger(const std::filesystem::path& dic_dir) : impl_(std::make_unique<Impl>()) {
    // As in transformers' MecabTokenizer: -d <dicdir> -r <dicdir>/mecabrc.
    // mecab_new2() splits its argument on spaces and ignores quotes, so a
    // dictionary path that contains a space cannot be used with it; pass the
    // arguments separately to mecab_new() instead.
    const std::filesystem::path mecabrc = dic_dir / "mecabrc";
    std::vector<std::string> arguments{"mecab", "-d", dic_dir.u8string()};
    if (std::filesystem::exists(mecabrc)) {
        arguments.push_back("-r");
        arguments.push_back(mecabrc.u8string());
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) argv.push_back(argument.data());
    impl_->tagger = mecab_new(static_cast<int>(argv.size()), argv.data());
    if (!impl_->tagger)
        throw std::runtime_error("could not initialize MeCab with \"" + dic_dir.u8string() + "\"");
}

MecabTagger::~MecabTagger() = default;

std::vector<ustring> MecabTagger::surfaces(const ustring& text) const {
    const std::string utf8 = to_utf8(text);
    std::vector<ustring> result;
    const mecab_node_t* node = mecab_sparse_tonode2(impl_->tagger, utf8.c_str(), utf8.size());
    for (; node; node = node->next) {
        // Keep the morphemes only; fugashi does not yield the BOS/EOS nodes.
        if (node->stat == MECAB_NOR_NODE || node->stat == MECAB_UNK_NODE)
            result.push_back(from_utf8(std::string_view(node->surface, node->length)));
    }
    return result;
}

}  // namespace hiragana
