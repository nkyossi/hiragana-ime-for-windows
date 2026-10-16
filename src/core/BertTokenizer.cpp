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

#include "BertTokenizer.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "MecabTagger.h"
#include "Nfkc.h"

namespace hiragana {

namespace {

constexpr std::size_t kMaxInputCharsPerWord = 100;

// Python's str.isspace() for the characters str.split() treats as whitespace.
bool is_space(char32_t c) {
    switch (c) {
        case U'\t':
        case U'\n':
        case U'\v':
        case U'\f':
        case U'\r':
        case U'\x1c':
        case U'\x1d':
        case U'\x1e':
        case U'\x1f':
        case U' ':
        case U'\x85':
        case U'\xa0':
            return true;
        default:
            break;
    }
    return (0x1680 <= c && c <= 0x1680) || (0x2000 <= c && c <= 0x200a) || c == 0x2028 ||
           c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

// Python's text.split() (runs of whitespace are dropped).
std::vector<ustring> whitespace_tokenize(const ustring& text) {
    std::vector<ustring> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && is_space(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && !is_space(text[i])) ++i;
        if (start < i) tokens.push_back(text.substr(start, i - start));
    }
    return tokens;
}

}  // namespace

BertTokenizer::BertTokenizer(const std::filesystem::path& vocab_file,
                             const std::filesystem::path& mecab_dic_dir) {
    load_vocab(vocab_file);
    mecab_ = std::make_unique<MecabTagger>(mecab_dic_dir);
    const auto special = [this](const char* name, int64_t fallback) {
        auto it = vocab_.find(from_utf8(name));
        return it == vocab_.end() ? fallback : it->second;
    };
    pad_ = special("[PAD]", 0);
    unk_ = special("[UNK]", 1);
    cls_ = special("[CLS]", 2);
    sep_ = special("[SEP]", 3);
    mask_ = special("[MASK]", 4);
    for (const char* name : {"[PAD]", "[UNK]", "[CLS]", "[SEP]", "[MASK]"}) {
        const ustring token = from_utf8(name);
        if (vocab_.find(token) != vocab_.end()) added_tokens_.insert(token);
    }
}

BertTokenizer::~BertTokenizer() = default;

void BertTokenizer::load_vocab(const std::filesystem::path& vocab_file) {
    std::ifstream in(vocab_file, std::ios::binary);
    if (!in)
        throw std::runtime_error("could not open the vocabulary \"" + vocab_file.u8string() + "\"");
    static const ustring kBom = U"\uFEFF";
    std::string line;
    int64_t id = 0;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        ustring token = from_utf8(line);
        if (id == 0 && starts_with(token, kBom)) token = token.substr(1);
        vocab_[std::move(token)] = id;
        ++id;
    }
}

bool BertTokenizer::is_in_vocab(const ustring& token) const {
    return vocab_.find(token) != vocab_.end();
}

int64_t BertTokenizer::token_id(const ustring& token) const {
    auto it = vocab_.find(token);
    return it == vocab_.end() ? unk_ : it->second;
}

std::vector<ustring> BertTokenizer::wordpiece(const ustring& text) const {
    static const ustring kUnk = U"[UNK]";
    static const ustring kPrefix = U"##";
    std::vector<ustring> output;
    for (const ustring& token : whitespace_tokenize(text)) {
        if (kMaxInputCharsPerWord < token.size()) {
            output.push_back(kUnk);
            continue;
        }
        bool is_bad = false;
        std::size_t start = 0;
        std::vector<ustring> sub_tokens;
        while (start < token.size()) {
            std::size_t end = token.size();
            ustring current;
            bool found = false;
            while (start < end) {
                ustring substr = token.substr(start, end - start);
                if (0 < start) substr = kPrefix + substr;
                if (is_in_vocab(substr)) {
                    current = std::move(substr);
                    found = true;
                    break;
                }
                --end;
            }
            if (!found) {
                is_bad = true;
                break;
            }
            sub_tokens.push_back(std::move(current));
            start = end;
        }
        if (is_bad)
            output.push_back(kUnk);
        else
            output.insert(output.end(), sub_tokens.begin(), sub_tokens.end());
    }
    return output;
}

std::vector<ustring> BertTokenizer::split_added_tokens(const ustring& text) const {
    std::vector<ustring> parts;
    ustring pending;
    std::size_t i = 0;
    while (i < text.size()) {
        // The longest added token that starts here (as tokens_trie.split()).
        const ustring* match = nullptr;
        for (const ustring& token : added_tokens_) {
            if (text.compare(i, token.size(), token) == 0 &&
                (match == nullptr || match->size() < token.size()))
                match = &token;
        }
        if (match != nullptr) {
            if (!pending.empty()) {
                parts.push_back(pending);
                pending.clear();
            }
            parts.push_back(*match);
            i += match->size();
        } else {
            pending.push_back(text[i]);
            ++i;
        }
    }
    if (!pending.empty()) parts.push_back(pending);
    return parts;
}

std::vector<ustring> BertTokenizer::tokenize(const ustring& text) const {
    std::vector<ustring> tokens;
    for (const ustring& part : split_added_tokens(text)) {
        if (is_added_token(part)) {
            // Kept whole, as in PreTrainedTokenizer.tokenize().
            tokens.push_back(part);
            continue;
        }
        const ustring normalized = nfkc(part);
        const std::vector<ustring> words = mecab_->surfaces(normalized);
        for (const ustring& word : words) {
            std::vector<ustring> pieces = wordpiece(word);
            tokens.insert(tokens.end(), std::make_move_iterator(pieces.begin()),
                          std::make_move_iterator(pieces.end()));
        }
    }
    return tokens;
}

std::vector<int64_t> BertTokenizer::encode(const ustring& text) const {
    std::vector<int64_t> ids;
    ids.push_back(cls_);
    for (const ustring& token : tokenize(text)) ids.push_back(token_id(token));
    ids.push_back(sep_);
    return ids;
}

std::vector<std::vector<int64_t>> BertTokenizer::encode_batch(
    const std::vector<ustring>& texts) const {
    std::vector<std::vector<int64_t>> batch;
    std::size_t longest = 0;
    for (const ustring& text : texts) {
        batch.push_back(encode(text));
        longest = std::max(longest, batch.back().size());
    }
    for (std::vector<int64_t>& ids : batch) ids.resize(longest, pad_);
    return batch;
}

std::vector<std::vector<int64_t>> BertTokenizer::attention_mask(
    const std::vector<std::vector<int64_t>>& batch) const {
    std::vector<std::vector<int64_t>> masks;
    masks.reserve(batch.size());
    for (const std::vector<int64_t>& ids : batch) {
        masks.emplace_back(ids.size(), 1);
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == pad_) masks.back()[i] = 0;
    }
    return masks;
}

}  // namespace hiragana
