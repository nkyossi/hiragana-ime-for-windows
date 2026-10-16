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

// Port of ibus-hiragana/engine/llm.py.  The comments keep the structure of the
// original so that the two can be compared line by line.

#include "LlamaAssist.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

#include "BertTokenizer.h"
#include "Json.h"
#include "OnnxBert.h"

namespace hiragana {

namespace {

constexpr index_t kMaxCandidates = 10;
// config.max_position_embeddings of bert-base-japanese-v3.
constexpr index_t kMaxPositionEmbeddings = 512;

// llm.py HIRAGANA (note that it also contains '・', 'ー', 'ゝ' and 'ゞ').
const ustring kHiragana =
    U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわ"
    U"ゐゑをんゔがぎぐげござじずぜぞだぢづでどばびぶべぼぁぃぅぇぉゃゅょっゎぱぴぷぺぽ・ーゝゞ";
// The trailing character class of llm.py RE_SUFFIX.
const ustring kSuffixMarkers = U"1iIkKgsStnbmrwW235";

bool is_hiragana(char32_t c) { return contains(kHiragana, c); }

// len() of Text.h is for ustring; the port also counts vector elements.
template <typename T>
index_t len(const std::vector<T>& values) {
    return static_cast<index_t>(values.size());
}
using ::hiragana::len;  // keep the ustring overload in the same overload set

// The .start() of llm.py RE_SUFFIX.search(stem): the start of the trailing run
// of "hiragana" characters, optionally followed by one marker character.
index_t suffix_start(const ustring& stem) {
    index_t start = len(stem);
    if (0 < start && contains(kSuffixMarkers, stem[static_cast<std::size_t>(start - 1)])) --start;
    while (0 < start && is_hiragana(stem[static_cast<std::size_t>(start - 1)])) --start;
    return start;
}

// len(RE_PREFIX.match(stem).group()) of llm.py: the leading run of hiragana.
index_t prefix_length(const ustring& stem) {
    index_t n = 0;
    while (n < len(stem) && is_hiragana(stem[static_cast<std::size_t>(n)])) ++n;
    return n;
}

std::vector<ustring> split(const ustring& text, char32_t separator) {
    std::vector<ustring> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t position = text.find(separator, start);
        if (position == ustring::npos) {
            fields.push_back(text.substr(start));
            break;
        }
        fields.push_back(text.substr(start, position - start));
        start = position + 1;
    }
    return fields;
}

// Python str.strip(chars).
ustring strip(const ustring& text, ustring_view chars) {
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && chars.find(text[start]) != ustring_view::npos) ++start;
    while (end > start && chars.find(text[end - 1]) != ustring_view::npos) --end;
    return text.substr(start, end - start);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not open \"" + path.u8string() + "\"");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<ustring> split_lines(const std::string& text) {
    std::vector<ustring> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t position = text.find('\n', start);
        std::string line =
            position == std::string::npos ? text.substr(start) : text.substr(start, position - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(from_utf8(line));
        if (position == std::string::npos) break;
        start = position + 1;
    }
    return lines;
}

template <typename T>
std::vector<T> vec_slice(const std::vector<T>& values, index_t a, index_t b) {
    index_t n = static_cast<index_t>(values.size());
    if (a < 0) a = 0;
    if (b > n) b = n;
    if (a >= b) return std::vector<T>();
    return std::vector<T>(values.begin() + a, values.begin() + b);
}

std::vector<std::vector<int64_t>> transpose(const std::vector<std::vector<int64_t>>& rows) {
    std::vector<std::vector<int64_t>> result;
    if (rows.empty()) return result;
    const std::size_t width = rows[0].size();
    result.assign(width, std::vector<int64_t>(rows.size(), 0));
    for (std::size_t r = 0; r < rows.size(); ++r)
        for (std::size_t c = 0; c < width; ++c) result[c][r] = rows[r][c];
    return result;
}

// `ids` with (mask_token_id, sep_token_id) appended, and the left truncation
// of llm.py when the sequence is longer than the model's maximum.
std::vector<int64_t> truncate_ids(const std::vector<int64_t>& ids, index_t offset, int64_t cls_token_id,
                                  int64_t mask_token_id, int64_t sep_token_id) {
    std::vector<int64_t> truncated = ids;
    truncated.push_back(mask_token_id);
    truncated.push_back(sep_token_id);
    if (0 < offset) {
        std::vector<int64_t> short_ids{cls_token_id};
        const std::vector<int64_t> rest = vec_slice(truncated, 1 + offset, len(truncated));
        short_ids.insert(short_ids.end(), rest.begin(), rest.end());
        return short_ids;
    }
    return truncated;
}

// len(set(ids)) != 1
bool all_same(const std::vector<int64_t>& ids) {
    for (std::size_t i = 1; i < ids.size(); ++i)
        if (ids[i] != ids[0]) return false;
    return true;
}

// The `mask_token_index` of llm.py's assist()/assist_yougen().
index_t find_mask_token_index(const std::vector<std::vector<int64_t>>& transposed) {
    index_t index = len(transposed) - 1;
    for (index_t i = 0; i < len(transposed); ++i) {
        if (!all_same(transposed[static_cast<std::size_t>(i)])) {
            index = i;
            break;
        }
    }
    if (index == len(transposed) - 1) index = len(transposed) - 2;
    return index;
}

}  // namespace

LlamaAssist::LlamaAssist(std::shared_ptr<OnnxBert> model,
                         std::shared_ptr<const BertTokenizer> tokenizer,
                         const std::filesystem::path& dic_dir)
    : model_(std::move(model)), tokenizer_(std::move(tokenizer)) {
    load_yougen(dic_dir / "yougen_token.dic");
    load_katuyou(dic_dir / "katuyou_token.dic");
    load_kango(dic_dir / "kango_token.json");
}

LlamaAssist::~LlamaAssist() = default;

std::shared_ptr<Model> LlamaAssist::create(bool enabled, const std::string& device,
                                           const std::filesystem::path& model_file,
                                           const std::filesystem::path& vocab_file,
                                           const std::filesystem::path& mecab_dic_dir,
                                           const std::filesystem::path& dic_dir, int threads,
                                           std::string* error) {
    auto fail = [error](const std::string& message) -> std::shared_ptr<Model> {
        if (error) *error = message;
        return nullptr;
    };
    if (!enabled) return fail("disabled");
    for (const auto& file : {model_file, vocab_file, mecab_dic_dir / "sys.dic", dic_dir / "yougen_token.dic"}) {
        if (!std::filesystem::exists(file)) return fail("missing \"" + file.u8string() + "\"");
    }
    try {
        auto model = std::make_shared<OnnxBert>(model_file, threads, device == "cuda");
        auto tokenizer = std::make_shared<BertTokenizer>(vocab_file, mecab_dic_dir);
        return std::make_shared<LlamaAssist>(std::move(model), std::move(tokenizer), dic_dir);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

std::string LlamaAssist::device_type() const { return model_->device_type(); }

std::vector<double> LlamaAssist::run_softmax(const std::vector<int64_t>& input_ids,
                                             index_t position) const {
    const std::vector<std::vector<float>> logits = model_->run(input_ids);
    if (position < 0 || static_cast<index_t>(logits.size()) <= position) return std::vector<double>();
    return softmax(logits[static_cast<std::size_t>(position)]);
}

bool LlamaAssist::is_kango_verb(const std::vector<int64_t>& ids, index_t i, const ustring& yomi,
                                const ustring& word) const {
    auto y = kango_tokens_.find(yomi);
    if (y == kango_tokens_.end()) return false;
    if (y->second.find(word) == y->second.end()) return false;
    if (static_cast<std::size_t>(i + 1) >= ids.size()) return false;
    const int64_t next = ids[static_cast<std::size_t>(i + 1)];
    if (next != tokenizer_->sep_token_id() && next != tokenizer_->pad_token_id()) return false;
    if (ids[static_cast<std::size_t>(i)] == tokenizer_->unk_token_id()) return false;
    return true;
}

double LlamaAssist::yougen_probability(const std::vector<double>& probabilities,
                                       const ustring& yomi) const {
    auto it = yougen_token_groups_.find(yomi);
    if (it == yougen_token_groups_.end()) return 0.0;
    double best = 0.0;
    bool first = true;
    for (const std::vector<int64_t>& group : it->second) {
        double sum = 0.0;
        for (int64_t token : group) sum += probabilities[static_cast<std::size_t>(token)];
        best = first ? sum : std::max(best, sum);
        first = false;
    }
    return best;
}

bool LlamaAssist::match(const ustring& token, const ustring& word, const ustring& conj) {
    if (word == token) return true;
    if (starts_with(word, token)) {
        if (0 < len(conj) && (last(conj) == U's' || last(conj) == U'S') &&
            (ends_with(word, U"しい") || ends_with(word, U"しく")))
            return false;
        return true;
    }
    if (starts_with(token, word)) return true;
    return false;
}

// The probability of a kango verb's token group.
static double group_sum(const std::vector<double>& probabilities,
                        const std::vector<int64_t>& tokens) {
    double sum = 0.0;
    for (int64_t token : tokens) sum += probabilities[static_cast<std::size_t>(token)];
    return sum;
}

std::map<index_t, double> LlamaAssist::assist(const ustring& prefix, const ustring& yomi,
                                              const std::vector<ustring>& words_in) {
    std::vector<ustring> yougen_yomi;
    std::vector<ustring> yougen_list;
    index_t pos_yougen = -1;
    if (find(yomi, U'―') < 0) {
        // Check that yougen exists in candidates.
        for (index_t i = 0; i < len(words_in); ++i) {
            const ustring& word = words_in[static_cast<std::size_t>(i)];
            if (word.empty() || last(word) != U'―') continue;
            pos_yougen = i;
            for (index_t j = 0; j < len(word) - 1; ++j) {
                const ustring suffix = slice_from(word, j);
                if (yougen_tokens_.find(suffix) != yougen_tokens_.end()) {
                    yougen_list.push_back(slice_to(word, j) + U"[UNK]");
                    yougen_yomi.push_back(suffix);
                }
            }
            break;
        }
    }

    std::vector<ustring> words;
    if (kMaxCandidates < len(words_in))
        words.assign(words_in.begin(), words_in.begin() + kMaxCandidates);
    else
        words = words_in;
    const index_t pos_cand = len(words);
    if (0 <= pos_yougen) {
        if (pos_yougen < pos_cand) words[static_cast<std::size_t>(pos_yougen)] = U"[UNK]";
        words.insert(words.end(), yougen_list.begin(), yougen_list.end());
    }

    std::vector<ustring> inputs;
    inputs.reserve(words.size());
    for (const ustring& word : words) inputs.push_back(prefix + word);

    const std::vector<std::vector<int64_t>> rows = tokenizer_->encode_batch(inputs);
    if (rows.empty()) return std::map<index_t, double>();
    const std::vector<std::vector<int64_t>> transposed = transpose(rows);

    const index_t mask_token_index = find_mask_token_index(transposed);
    index_t offset = 0;
    if (kMaxPositionEmbeddings < len(transposed)) offset = len(transposed) - kMaxPositionEmbeddings;

    const std::vector<int64_t> ids = truncate_ids(
        vec_slice(rows[0], 0, mask_token_index), offset, tokenizer_->cls_token_id(),
        tokenizer_->mask_token_id(), tokenizer_->sep_token_id());
    const std::vector<int64_t>& token_ids = transposed[static_cast<std::size_t>(mask_token_index)];

    std::vector<double> probabilities = run_softmax(ids, mask_token_index - offset);

    // 漢語動詞の出現確率を計算
    for (index_t i = 0; i < pos_cand; ++i) {
        const std::vector<int64_t>& row = rows[static_cast<std::size_t>(i)];
        if (is_kango_verb(row, mask_token_index, yomi, words[static_cast<std::size_t>(i)])) {
            const int64_t id = tokenizer_->token_id(words[static_cast<std::size_t>(i)]);
            const std::vector<int64_t>& tokens = kango_tokens_.at(yomi).at(words[static_cast<std::size_t>(i)]);
            probabilities[static_cast<std::size_t>(id)] = group_sum(probabilities, tokens);
        }
    }

    // 用言の活用形の出現確率を計算
    std::vector<double> yougen_p;
    yougen_p.reserve(static_cast<std::size_t>(std::max<index_t>(0, len(words) - pos_cand)));
    for (index_t i = pos_cand; i < len(words); ++i) {
        if (rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(mask_token_index)] ==
            tokenizer_->unk_token_id()) {
            const ustring& yy = yougen_yomi[static_cast<std::size_t>(i - pos_cand)];
            if (yougen_tokens_.find(yy) != yougen_tokens_.end())
                yougen_p.push_back(yougen_probability(probabilities, yy));
            else
                yougen_p.push_back(0.0);
        } else {
            const int64_t id = transposed[static_cast<std::size_t>(mask_token_index)]
                                        [static_cast<std::size_t>(i)];
            yougen_p.push_back(probabilities[static_cast<std::size_t>(id)]);
        }
    }

    // probabilities: ids[mask_token_index]にくるwordsの確率
    std::vector<double> probs;
    probs.reserve(std::max<index_t>(len(token_ids), pos_cand) +
                  static_cast<index_t>(yougen_p.size()));
    for (int64_t token : token_ids) probs.push_back(probabilities[static_cast<std::size_t>(token)]);
    probs = vec_slice(probs, 0, pos_cand);
    probs.insert(probs.end(), yougen_p.begin(), yougen_p.end());

    double p_max = 0.0;
    for (index_t i = mask_token_index + 1; i < len(transposed) - 1; ++i) {
        std::set<index_t> calculated;
        for (index_t j = 0; j < len(rows); ++j) {
            if (calculated.count(j) != 0) continue;
            const std::vector<int64_t>& row = rows[static_cast<std::size_t>(j)];
            const int64_t id_i = row[static_cast<std::size_t>(i)];
            if (id_i == tokenizer_->sep_token_id() || id_i == tokenizer_->pad_token_id()) {
                if (j != pos_yougen) p_max = std::max(p_max, probs[static_cast<std::size_t>(j)]);
                continue;
            }
            if (probs[static_cast<std::size_t>(j)] <= p_max) continue;
            const std::vector<int64_t> next_ids = truncate_ids(
                vec_slice(row, 0, i), offset, tokenizer_->cls_token_id(), tokenizer_->mask_token_id(),
                tokenizer_->sep_token_id());
            // p: ids[i]にくるトークンの確率
            const std::vector<double> p = run_softmax(next_ids, i - offset);

            if (pos_cand <= j && id_i == tokenizer_->unk_token_id()) {
                const ustring& yy = yougen_yomi[static_cast<std::size_t>(j - pos_cand)];
                if (yougen_tokens_.find(yy) != yougen_tokens_.end())
                    probs[static_cast<std::size_t>(j)] *= yougen_probability(p, yy);
                else
                    probs[static_cast<std::size_t>(j)] = 0.0;
            } else {
                if (is_kango_verb(row, i, yomi, words[static_cast<std::size_t>(j)]))
                    probs[static_cast<std::size_t>(j)] *=
                        group_sum(p, kango_tokens_.at(yomi).at(words[static_cast<std::size_t>(j)]));
                else
                    probs[static_cast<std::size_t>(j)] *=
                        p[static_cast<std::size_t>(transposed[static_cast<std::size_t>(i)]
                                                            [static_cast<std::size_t>(j)])];
                for (index_t k = j + 1; k < len(inputs); ++k) {
                    if (vec_slice(row, mask_token_index, i) ==
                        vec_slice(rows[static_cast<std::size_t>(k)], mask_token_index, i)) {
                        if (is_kango_verb(row, i, yomi, words[static_cast<std::size_t>(k)]))
                            probs[static_cast<std::size_t>(k)] *=
                                group_sum(p, kango_tokens_.at(yomi).at(
                                                words[static_cast<std::size_t>(k)]));
                        else
                            probs[static_cast<std::size_t>(k)] *=
                                p[static_cast<std::size_t>(transposed[static_cast<std::size_t>(i)]
                                                                    [static_cast<std::size_t>(k)])];
                        calculated.insert(k);
                    }
                }
            }
        }
    }

    std::map<index_t, double> p_dict;
    if (pos_yougen < 0) {
        for (index_t i = 0; i < len(probs); ++i) p_dict[i] = probs[static_cast<std::size_t>(i)];
    } else {
        for (index_t i = 0; i < pos_cand; ++i) p_dict[i] = probs[static_cast<std::size_t>(i)];
        // Note probabilities[pos_cand:] can be empty.
        double best = 0.0;
        for (index_t i = pos_cand; i < len(probs); ++i)
            best = std::max(best, probs[static_cast<std::size_t>(i)]);
        p_dict[pos_yougen] = best;
    }
    return p_dict;
}

std::map<index_t, double> LlamaAssist::assist_yougen(const ustring& prefix, const ustring& yomi,
                                                     const std::vector<ustring>& stem_list) {
    if (len(stem_list) == 1) return {{0, 1.0}};

    std::vector<ustring> yougen_list;
    std::vector<index_t> shrink_list;
    std::vector<index_t> shrink_index;

    const index_t pos = rfind(yomi, U"―");
    for (index_t i = 0; i < pos; ++i) {
        if (yougen_tokens_.find(slice(yomi, i, pos + 1)) != yougen_tokens_.end()) {
            yougen_list.push_back(slice_to(yomi, i) + U"[UNK]");
            shrink_list.push_back(i);
        }
    }
    if (yougen_list.empty()) return std::map<index_t, double>();

    for (const ustring& stem : stem_list) {
        const index_t shrink = prefix_length(stem);
        if (shrink == 0) {
            shrink_index.push_back(0);
        } else {
            auto it = std::find(shrink_list.begin(), shrink_list.end(), shrink);
            shrink_index.push_back(it == shrink_list.end() ? 0 : static_cast<index_t>(it - shrink_list.begin()));
        }
    }

    std::vector<ustring> inputs;
    inputs.reserve(yougen_list.size());
    for (const ustring& yougen : yougen_list) inputs.push_back(prefix + yougen);

    const std::vector<std::vector<int64_t>> rows = tokenizer_->encode_batch(inputs);
    if (rows.empty()) return std::map<index_t, double>();
    const std::vector<std::vector<int64_t>> transposed = transpose(rows);

    const index_t mask_token_index = find_mask_token_index(transposed);
    index_t offset = 0;
    if (kMaxPositionEmbeddings < len(transposed)) offset = len(transposed) - kMaxPositionEmbeddings;

    const std::vector<int64_t> ids = truncate_ids(
        vec_slice(rows[0], 0, mask_token_index), offset, tokenizer_->cls_token_id(),
        tokenizer_->mask_token_id(), tokenizer_->sep_token_id());
    const std::vector<double> probabilities = run_softmax(ids, mask_token_index - offset);

    std::vector<double> prefix_p(yougen_list.size(), 1.0);
    std::vector<double> yougen_p(stem_list.size(), 0.0);

    // The probability of a stem's conjugation group; returns false when no
    // token matched (llm.py assigns only `if v:`).
    const auto katuyou_probabilities = [this, &yomi, pos](const ustring& stem,
                                                          const std::vector<double>& probabilities,
                                                          double& out) {
        auto it = katuyou_tokens_.find(stem);
        if (it == katuyou_tokens_.end()) return false;
        const index_t suffix = suffix_start(stem);
        const ustring word = slice_to(stem, suffix) + slice_from(yomi, pos + 1);
        const ustring conj = slice_from(stem, suffix);
        bool found = false;
        for (const ustring& token : it->second) {
            if (!match(token, word, conj)) continue;
            const double value = probabilities[static_cast<std::size_t>(tokenizer_->token_id(token))];
            out = found ? std::max(out, value) : value;
            found = true;
        }
        return found;
    };

    for (index_t i = 0; i < len(yougen_list); ++i) {
        if (rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(mask_token_index)] ==
            tokenizer_->unk_token_id()) {
            for (index_t j = 0; j < len(stem_list); ++j) {
                if (shrink_index[static_cast<std::size_t>(j)] != i) continue;
                const ustring stem =
                    slice_from(stem_list[static_cast<std::size_t>(j)],
                               shrink_list[static_cast<std::size_t>(i)]);
                double value = 0.0;
                if (katuyou_probabilities(stem, probabilities, value))
                    yougen_p[static_cast<std::size_t>(j)] = value;
            }
        } else {
            const int64_t id = transposed[static_cast<std::size_t>(mask_token_index)]
                                        [static_cast<std::size_t>(i)];
            prefix_p[static_cast<std::size_t>(i)] = probabilities[static_cast<std::size_t>(id)];
        }
    }

    for (index_t i = mask_token_index + 1; i < len(transposed) - 1; ++i) {
        for (index_t j = 0; j < len(rows); ++j) {
            const std::vector<int64_t>& row = rows[static_cast<std::size_t>(j)];
            const int64_t id_i = row[static_cast<std::size_t>(i)];
            if (id_i == tokenizer_->sep_token_id() || id_i == tokenizer_->pad_token_id()) continue;
            const std::vector<int64_t> next_ids = truncate_ids(
                vec_slice(row, 0, i), offset, tokenizer_->cls_token_id(), tokenizer_->mask_token_id(),
                tokenizer_->sep_token_id());
            const std::vector<double> p = run_softmax(next_ids, i - offset);
            if (id_i == tokenizer_->unk_token_id()) {
                for (index_t k = 0; k < len(stem_list); ++k) {
                    if (shrink_index[static_cast<std::size_t>(k)] != j) continue;
                    const ustring stem =
                        slice_from(stem_list[static_cast<std::size_t>(k)],
                                   shrink_list[static_cast<std::size_t>(j)]);
                    double value = 0.0;
                    if (katuyou_probabilities(stem, probabilities, value))
                        yougen_p[static_cast<std::size_t>(k)] = value;
                }
            } else {
                prefix_p[static_cast<std::size_t>(j)] *=
                    p[static_cast<std::size_t>(transposed[static_cast<std::size_t>(i)]
                                                        [static_cast<std::size_t>(j)])];
            }
        }
    }

    std::map<index_t, double> p_dict;
    for (index_t i = 0; i < len(stem_list); ++i)
        p_dict[i] = prefix_p[static_cast<std::size_t>(shrink_index[static_cast<std::size_t>(i)])] *
                    yougen_p[static_cast<std::size_t>(i)];
    return p_dict;
}

void LlamaAssist::load_yougen(const std::filesystem::path& path) {
    for (const ustring& line : split_lines(read_file(path))) {
        if (line.empty() || line[0] == U';') continue;
        const index_t space = find(line, U' ');
        if (space < 0) continue;
        const ustring yomi = slice_to(line, space);
        const std::vector<ustring> tokens = split(strip(slice_from(line, space + 1), U" \n/"), U'/');
        std::vector<int64_t> ids;
        std::map<ustring, std::vector<int64_t>> groups;
        for (const ustring& token : tokens) {
            const int64_t id = tokenizer_->token_id(token);
            ids.push_back(id);
            const index_t first = [&token] {
                for (index_t i = 0; i < len(token); ++i)
                    if (is_hiragana(token[static_cast<std::size_t>(i)])) return i;
                return len(token);
            }();
            groups[slice_to(token, first)].push_back(id);
        }
        yougen_tokens_[yomi] = std::move(ids);
        std::vector<std::vector<int64_t>> group_list;
        for (auto& [kanji, group] : groups) group_list.push_back(std::move(group));
        yougen_token_groups_[yomi] = std::move(group_list);
    }
}

void LlamaAssist::load_katuyou(const std::filesystem::path& path) {
    for (const ustring& line : split_lines(read_file(path))) {
        if (line.empty() || line[0] == U';') continue;
        const index_t space = find(line, U' ');
        if (space < 0) continue;
        katuyou_tokens_[slice_to(line, space)] =
            split(strip(slice_from(line, space + 1), U" \n/"), U'/');
    }
}

void LlamaAssist::load_kango(const std::filesystem::path& path) {
    const json::Value root = json::parse(read_file(path));
    for (const auto& [yomi, kanji_map] : root.as_object()) {
        std::map<ustring, std::vector<int64_t>> converted;
        for (const auto& [kanji, tokens] : kanji_map.as_object()) {
            std::vector<int64_t> ids;
            for (const json::Value& token : tokens.as_array())
                ids.push_back(tokenizer_->token_id(from_utf8(token.as_string())));
            converted[from_utf8(kanji)] = std::move(ids);
        }
        kango_tokens_[from_utf8(yomi)] = std::move(converted);
    }
}

}  // namespace hiragana
