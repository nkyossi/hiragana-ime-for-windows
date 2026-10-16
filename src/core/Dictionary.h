// Hiragana IME for Windows
//
// Copyright (c) 2017-2026 Esrille Inc. (ibus-hiragana)
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

// Port of engine/dictionary.py without the LLM-assisted lookup.
//
// Differences from the original:
// - The working dictionary owns copies of the candidate lists, so a learned
//   order is written to the history file. (In dictionary.py the working
//   dictionary is a shallow copy of the base dictionary, and confirm() can
//   modify the base list in place, which hides the change from save_orders().)
// - Several processes can use the same history file. save_orders() merges the
//   entries this process changed into the file on disk, and sync() picks up
//   changes saved by other processes.
// - The system and user dictionaries are parsed once per process and shared
//   read-only by all Dictionary objects; each object keeps only its own
//   changes (the input history and the learned orders) on top of them.

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "Model.h"
#include "Text.h"

namespace hiragana {

class Dictionary {
public:
    struct Config {
        std::filesystem::path system_dir;  // directory holding the system *.dic files
        std::filesystem::path user_dir;    // user data directory (my.dic, dic/<system>)
        std::string system = "restrained.9.dic";
        std::string user = "my.dic";
        bool clear_history = false;
        bool permissible = false;
    };

    explicit Dictionary(const Config& config);

    void reset();
    ustring set_current_to_next();
    ustring set_current_to_previous();
    ustring current() const;
    void set_current(index_t index);
    const ustring& reading() const { return yomi_; }
    std::vector<ustring> cand() const;

    ustring lookup(const ustring& text, index_t pos, index_t anchor = 0);
    // LLM-assisted lookup; the port of dictionary.py assisted_lookup().  A
    // reading may be "shrunk" (only part of it is converted) and the candidate
    // that best fits the surrounding text is suggested in `suggested`.
    ustring assisted_lookup(Model& model, const ustring& text, index_t pos, index_t anchor, index_t& suggested);
    bool is_complete() const;
    // Returns the reading and the word of candidate `no`.
    std::pair<ustring, ustring> get_stem(index_t no) const;
    // Returns the word of every candidate (the reading is common).
    std::vector<ustring> get_stem_list() const;
    // Words the user has rejected for a reading (dictionary.py _rejected).
    bool is_rejected(const ustring& yomi, const ustring& word) const;
    void reject(const ustring& yomi, const ustring& word);
    void accept(const ustring& yomi, const ustring& word);
    void remove_entry(const ustring& yomi, const ustring& word);
    // The word registered by the last assisted_lookup() that only converts a
    // part of the reading.
    const ustring& shrunk() const { return shrunk_; }
    // Returns the index of the confirmed candidate.
    index_t confirm(const ustring& shrunk);
    void create_pseudo_candidate(const ustring& text);
    bool is_pseudo_candidate() const;
    void add_katakana(const ustring& word);
    void use_romazi(bool romazi) { romazi_ = romazi; }

    // Writes the input history if it has changed.
    void save_orders();
    // Reloads the input history if another process has saved it.
    void sync();

    // Exposed for tests.
    int match(const ustring& okuri, const ustring& yomi) const;
    static bool strdcmp(const ustring& okuri, const ustring& yomi);
    index_t max_len() const { return max_len_; }
    const std::vector<ustring>* entry(const ustring& yomi) const;
    const std::filesystem::path& orders_path() const { return orders_path_; }

private:
    using Dict = std::unordered_map<ustring, std::vector<ustring>>;

    // The base dictionaries (katakana.dic, the system and user dictionaries).
    struct Base {
        Dict dict;
        index_t max_len = 0;
    };

    // The working dictionary: the shared base plus the changes of this object.
    // An empty candidate list in own marks a removed entry.
    struct Table {
        const Dict* base = nullptr;
        Dict own;

        const std::vector<ustring>* get(const ustring& yomi) const;
        void put(const ustring& yomi, std::vector<ustring> words) { own[yomi] = std::move(words); }
        void erase(const ustring& yomi);
    };

    static std::shared_ptr<const Base> load_base(const Config& config);
    static void load_dict(Table& dic, const std::filesystem::path& path, index_t& max_len, bool& dirty,
                          bool version_checked = true);
    static void merge_entry(Table& dic, const ustring& yomi, const std::vector<ustring>& words, bool reorder_only,
                            index_t& max_len, bool& dirty);
    static void remove_entries(Table& dic, const ustring& yomi, const std::vector<ustring>& cand);
    bool lookup_next_taigen(const ustring& text, index_t start, index_t pos);
    void lookup_numeric(const ustring& text, index_t start, index_t pos, const ustring& numeric);
    bool lookup_next_yougen(const ustring& text, index_t start, index_t pos, index_t suffix);
    bool compare_okuri(const ustring& okuri, const ustring& yomi) const;
    void set_entry(const ustring& yomi, const std::vector<ustring>& cand);
    void write_orders(const std::filesystem::path& filename);
    void reload_history();
    std::filesystem::file_time_type history_time() const;

    std::shared_ptr<const Base> base_;
    Table dict_;
    index_t max_len_ = 0;

    ustring yomi_;
    index_t no_ = 0;
    std::vector<ustring> cand_;
    std::vector<index_t> order_;
    std::vector<int> completed_;
    ustring numeric_;
    bool dirty_ = false;
    bool romazi_ = true;

    ustring shrunk_;  // reading shrunk by the LLM (dictionary.py _shrunk)
    std::map<ustring, std::set<ustring>> rejected_;  // dictionary.py _rejected

    std::filesystem::path orders_path_;
    std::filesystem::file_time_type orders_time_{};
    std::set<ustring> touched_;  // entries changed since the last save
};

}  // namespace hiragana
