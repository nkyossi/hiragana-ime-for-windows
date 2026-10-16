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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "Text.h"

namespace hiragana {

class MecabTagger;

// Port of BertJapaneseTokenizer of tohoku-nlp/bert-base-japanese-v3 as used by
// ibus-hiragana/engine/llm.py: NFKC normalization, MeCab word splitting,
// WordPiece, and the [CLS]/[SEP]/[PAD] handling of PreTrainedTokenizer.__call__.
class BertTokenizer {
public:
    // vocab_file is the BERT vocab.txt; mecab_dic_dir is unidic-lite's dicdir.
    BertTokenizer(const std::filesystem::path& vocab_file, const std::filesystem::path& mecab_dic_dir);
    ~BertTokenizer();

    BertTokenizer(const BertTokenizer&) = delete;
    BertTokenizer& operator=(const BertTokenizer&) = delete;

    // tokenizer.tokenize(text): the subword tokens without the special tokens.
    std::vector<ustring> tokenize(const ustring& text) const;
    // tokenizer(text)['input_ids']: [CLS] ... [SEP].
    std::vector<int64_t> encode(const ustring& text) const;
    // tokenizer(texts, padding=True)['input_ids']: right padded with pad_token_id.
    std::vector<std::vector<int64_t>> encode_batch(const std::vector<ustring>& texts) const;
    // The attention mask that goes with encode_batch().
    std::vector<std::vector<int64_t>> attention_mask(
        const std::vector<std::vector<int64_t>>& batch) const;

    // The id of a token; unk_token_id() when the token is not in the vocabulary.
    int64_t token_id(const ustring& token) const;
    bool is_in_vocab(const ustring& token) const;

    int64_t cls_token_id() const { return cls_; }
    int64_t sep_token_id() const { return sep_; }
    int64_t pad_token_id() const { return pad_; }
    int64_t unk_token_id() const { return unk_; }
    int64_t mask_token_id() const { return mask_; }
    std::size_t vocab_size() const { return vocab_.size(); }

private:
    std::vector<ustring> wordpiece(const ustring& token) const;
    // PreTrainedTokenizer.tokenize(): split on the added tokens first, so that
    // e.g. "[UNK]" is kept whole instead of being sent to MeCab.
    std::vector<ustring> split_added_tokens(const ustring& text) const;
    bool is_added_token(const ustring& token) const { return added_tokens_.count(token) != 0; }
    void load_vocab(const std::filesystem::path& vocab_file);

    std::map<ustring, int64_t> vocab_;
    std::set<ustring> added_tokens_;
    std::unique_ptr<MecabTagger> mecab_;
    int64_t cls_ = 2;
    int64_t sep_ = 3;
    int64_t pad_ = 0;
    int64_t unk_ = 1;
    int64_t mask_ = 4;
};

}  // namespace hiragana
