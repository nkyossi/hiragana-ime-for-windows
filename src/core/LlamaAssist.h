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

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Model.h"
#include "Text.h"

namespace hiragana {

class BertTokenizer;
class OnnxBert;

// Port of LanguageModel of ibus-hiragana/engine/llm.py: it scores conversion
// candidates with a masked language model (BERT) and the token dictionaries
// yougen_token.dic, katuyou_token.dic and kango_token.json.
class LlamaAssist : public Model {
public:
    LlamaAssist(std::shared_ptr<OnnxBert> model, std::shared_ptr<const BertTokenizer> tokenizer,
                const std::filesystem::path& dic_dir);
    ~LlamaAssist() override;

    std::map<index_t, double> assist(const ustring& prefix, const ustring& yomi,
                                     const std::vector<ustring>& words) override;
    std::map<index_t, double> assist_yougen(const ustring& prefix, const ustring& yomi,
                                            const std::vector<ustring>& stems) override;
    std::string device_type() const override;

    // Loads the backend; returns null when it is disabled or its files are
    // missing.  model_file is the exported model.onnx, vocab_file the
    // vocabulary, mecab_dic_dir unidic-lite's dicdir, and dic_dir the directory
    // with the yougen/katuyou/kango token files.  device is "cpu" or "cuda";
    // threads is the number of threads ONNX Runtime may use.  error receives
    // the reason when it returns null.
    static std::shared_ptr<Model> create(bool enabled, const std::string& device,
                                         const std::filesystem::path& model_file,
                                         const std::filesystem::path& vocab_file,
                                         const std::filesystem::path& mecab_dic_dir,
                                         const std::filesystem::path& dic_dir, int threads = 1,
                                         std::string* error = nullptr);

private:
    std::vector<double> run_softmax(const std::vector<int64_t>& input_ids, index_t position) const;
    double yougen_probability(const std::vector<double>& probabilities, const ustring& yomi) const;
    bool is_kango_verb(const std::vector<int64_t>& ids, index_t i, const ustring& yomi,
                       const ustring& word) const;
    static bool match(const ustring& token, const ustring& word, const ustring& conj);

    void load_yougen(const std::filesystem::path& path);
    void load_katuyou(const std::filesystem::path& path);
    void load_kango(const std::filesystem::path& path);

    std::shared_ptr<OnnxBert> model_;
    std::shared_ptr<const BertTokenizer> tokenizer_;
    std::map<ustring, std::vector<int64_t>> yougen_tokens_;
    std::map<ustring, std::vector<std::vector<int64_t>>> yougen_token_groups_;
    std::map<ustring, std::vector<ustring>> katuyou_tokens_;
    std::map<ustring, std::map<ustring, std::vector<int64_t>>> kango_tokens_;
};

}  // namespace hiragana
