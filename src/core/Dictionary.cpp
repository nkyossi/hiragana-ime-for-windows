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

#include "Dictionary.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>

#include "KanaTables.h"
#include "Log.h"

namespace hiragana {

namespace fs = std::filesystem;
using namespace kana;

namespace {

const char kDictionaryVersion[] = "v1.0.0";

using Katuyou = std::vector<std::optional<ustring>>;
const std::nullopt_t None = std::nullopt;

const std::unordered_map<char32_t, Katuyou>& katuyou_table() {
    static const std::unordered_map<char32_t, Katuyou> table = {
        {U'1', {U"", U"る", U"れば", U"ろ", U"よう", U"て", U"た", U"な", U"た", U"ま", U"ず", None, None}},
        {U'i', {U"く", U"い", U"ければ", None, U"かろう", U"くて", U"かった", None, None, None, None, None, None, U"",
                U"さ", U"み", U"げ", U"そう"}},
        {U'I', {U"く", U"い", U"ければ", None, U"かろう", U"くて", U"かった", None, None, None, None, None, None, U"",
                U"さ", U"み", U"げ", U"そう", U"な"}},  // 小さい, 大きい
        {U'k', {U"き", U"く", U"けば", U"け", U"こう", U"いて", U"いた", U"かな", U"きた", U"きま", U"かず", U"かせ", U"かれ"}},
        {U'K', {U"き", U"く", U"けば", U"け", U"こう", U"って", U"った", U"かな", U"きた", U"きま", U"かず", U"かせ", U"かれ"}},  // 行く
        {U'g', {U"ぎ", U"ぐ", U"げば", U"げ", U"ごう", U"いで", U"いだ", U"がな", U"ぎた", U"ぎま", U"がず", U"がせ", U"がれ"}},
        {U's', {U"し", U"す", U"せば", U"せ", U"そう", U"して", U"した", U"さな", U"した", U"しま", U"さず", U"させ", U"され"}},
        {U'S', {U"し", U"する", U"すれば", U"しろ", U"しよう", U"して", U"した", U"しな", U"した", U"しま", U"せず", U"させ", U"され"}},  // 欲S
        {U't', {U"ち", U"つ", U"てば", U"て", U"とう", U"って", U"った", U"たな", U"ちた", U"ちま", U"たず", U"たせ", U"たれ"}},
        {U'n', {U"に", U"ぬ", U"ねば", U"ね", U"のう", U"んで", U"んだ", U"なな", U"にた", U"にま", U"なず", U"なせ", U"なれ"}},
        {U'b', {U"び", U"ぶ", U"べば", U"べ", U"ぼう", U"んで", U"んだ", U"ばな", U"びた", U"びま", U"ばず", U"ばせ", U"ばれ"}},
        {U'm', {U"み", U"む", U"めば", U"め", U"もう", U"んで", U"んだ", U"まな", U"みた", U"みま", U"まず", U"ませ", U"まれ"}},
        {U'r', {U"り", U"る", U"れば", U"れ", U"ろう", U"って", U"った", U"らな", U"りた", U"りま", U"らず", U"らせ", U"られ"}},
        {U'w', {U"い", U"う", U"えば", U"え", U"おう", U"って", U"った", U"わな", U"いた", U"いま", U"わず", U"わせ", U"われ"}},
        {U'W', {U"い", U"う", U"えば", U"え", U"おう", U"うて", U"うた", U"わな", U"いた", U"いま", U"わず", U"わせ", U"われ"}},  // 問う, 請う, 乞う, etc.
        {U'2', {U"", None, None, None, None, U"て", U"た", None, U"た", U"ま", None, None, None}},  // きて
        {U'3', {None, U"る", U"れば", None, None, None, None, None, None, None, None, None, None}},  // くる
        {U'5', {None, None, None, U"い", U"よう", None, None, U"な", None, None, U"ず", U"させ", U"られ"}},  // こい
    };
    return table;
}

// YOMI = ^#?[HIRAGANA]+―?$
bool is_valid_yomi(const ustring& yomi) {
    size_t i = 0;
    size_t n = yomi.size();
    if (i < n && yomi[i] == U'#') ++i;
    if (0 < n && yomi[n - 1] == DASH) --n;
    if (i >= n) return false;
    for (; i < n; ++i) {
        if (!is_hiragana(yomi[i])) return false;
    }
    return true;
}

// Start position of SUFFIX = [HIRAGANA]*[1iIkKgsStnbmrwW235]?$ found by re.search().
index_t suffix_start(const ustring& word) {
    index_t j = len(word);
    if (0 < j && contains(SUFFIX_CODES, word[j - 1])) --j;
    while (0 < j && is_hiragana(word[j - 1])) --j;
    return j;
}

ustring strip(const ustring& s, ustring_view chars) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && contains(chars, s[a])) ++a;
    while (a < b && contains(chars, s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::vector<ustring> split(const ustring& s, char32_t sep) {
    std::vector<ustring> parts;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(sep, start);
        if (pos == ustring::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

template <class T>
bool has(const std::vector<T>& v, const T& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

template <class T>
void remove_first(std::vector<T>& v, const T& x) {
    auto it = std::find(v.begin(), v.end(), x);
    if (it != v.end()) v.erase(it);
}

// Port of Python max(p_dict, key=p_dict.get): the first (smallest) index with
// the greatest probability.
index_t argmax(const std::map<index_t, double>& p_dict) {
    index_t best = 0;
    double best_value = 0;
    bool found = false;
    for (const auto& entry : p_dict) {
        if (!found || best_value < entry.second) {
            best = entry.first;
            best_value = entry.second;
            found = true;
        }
    }
    return best;
}

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

bool Dictionary::strdcmp(const ustring& okuri, const ustring& yomi) {
    if (yomi.empty()) return false;
    index_t last = len(yomi) - 1;
    if (len(okuri) <= last) return false;
    index_t pos = find(DAKUON, okuri[last]);
    if (0 <= pos) {
        ustring o = slice_to(okuri, last) + SEION[pos];
        if (o == yomi) return true;
    }
    return false;
}

bool Dictionary::compare_okuri(const ustring& okuri, const ustring& yomi) const {
    return romazi_ ? okuri == yomi : strdcmp(okuri, yomi);
}

const std::vector<ustring>* Dictionary::Table::get(const ustring& yomi) const {
    auto it = own.find(yomi);
    if (it != own.end()) return it->second.empty() ? nullptr : &it->second;
    if (base) {
        auto b = base->find(yomi);
        if (b != base->end()) return &b->second;
    }
    return nullptr;
}

void Dictionary::Table::erase(const ustring& yomi) {
    if (base && base->count(yomi))
        own[yomi].clear();
    else
        own.erase(yomi);
}

std::shared_ptr<const Dictionary::Base> Dictionary::load_base(const Config& config) {
    fs::path dir_path = config.system_dir;
    fs::path user_path;
    if (!config.user.empty() && config.user.find("..") == std::string::npos) {
        fs::path user = fs::u8path(config.user);
        user_path = user.is_absolute() ? user : config.user_dir / user;
    }
    bool permissible = config.permissible && config.system == "restrained.9.dic";

    // The cache key includes the time stamps so that an edited dictionary is reloaded.
    auto stamp = [](const fs::path& p) {
        std::error_code ec;
        auto t = fs::last_write_time(p, ec);
        return ec ? std::string("-") : std::to_string(t.time_since_epoch().count());
    };
    std::string key = dir_path.u8string() + "|" + config.system + "|" + stamp(dir_path / fs::u8path(config.system)) +
                      "|" + user_path.u8string() + "|" + stamp(user_path) + "|" + (permissible ? "p" : "-");

    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<const Base>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if (auto cached = cache[key].lock()) return cached;

    auto base = std::make_shared<Base>();
    Table table;
    bool dirty = false;
    // Load Katakana dictionary first so that Katakana words come after Kanji words.
    load_dict(table, dir_path / "katakana.dic", base->max_len, dirty);
    if (permissible) load_dict(table, dir_path / "permissible.dic", base->max_len, dirty);
    // Load system dictionary
    load_dict(table, dir_path / fs::u8path(config.system), base->max_len, dirty);
    // Load user dictionary
    if (!user_path.empty()) load_dict(table, user_path, base->max_len, dirty);
    base->dict = std::move(table.own);
    cache[key] = base;
    return base;
}

Dictionary::Dictionary(const Config& config) {
    LOG_DEBUG("Dictionary(\"%s\", \"%s\", %d, %d)", config.system.c_str(), config.user.c_str(),
              config.clear_history, config.permissible);

    base_ = load_base(config);

    // Create the working dictionary
    dict_.base = &base_->dict;
    max_len_ = base_->max_len;

    // Load input history
    orders_path_ = config.user_dir / "dic" / fs::u8path(config.system);
    std::error_code ec;
    fs::create_directories(orders_path_.parent_path(), ec);
    if (config.clear_history) {
        LOG_DEBUG("clear_history");
        std::ofstream f(orders_path_, std::ios::binary | std::ios::trunc);
        f << "; " << kDictionaryVersion << "\n";
    } else {
        load_dict(dict_, orders_path_, max_len_, dirty_, false);
    }
    orders_time_ = history_time();
}

void Dictionary::load_dict(Table& dic, const fs::path& path, index_t& max_len, bool& dirty,
                           bool version_checked) {
    bool reorder_only = !version_checked;
    auto content = read_file(path);
    if (!content) {
        LOG_WARNING("could not load \"%s\"", path.u8string().c_str());
        return;
    }
    const ustring version = from_utf8(std::string("; ") + kDictionaryVersion);
    ustring text = from_utf8(*content);
    for (ustring line : split(text, U'\n')) {
        line = strip(line, U" \r\n/");
        if (line.empty()) continue;
        if (line[0] == U';') {
            if (line == version) version_checked = true;
            continue;
        }
        if (!version_checked) continue;
        size_t space = line.find(U' ');
        if (space == ustring::npos) continue;
        ustring yomi = line.substr(0, space);
        std::vector<ustring> words = split(strip(line.substr(space + 1), U" \n/"), U'/');
        if (starts_with(yomi, U"-")) {
            remove_entries(dic, yomi.substr(1), words);
        } else {
            merge_entry(dic, yomi, words, reorder_only, max_len, dirty);
            if (last(yomi) == DASH) {
                merge_entry(dic, slice_to(yomi, len(yomi) - 1), {yomi}, reorder_only, max_len, dirty);
            }
        }
    }
    LOG_DEBUG("Loaded %s", path.u8string().c_str());
}

void Dictionary::merge_entry(Table& dic, const ustring& yomi, const std::vector<ustring>& words_in,
                             bool reorder_only, index_t& max_len, bool& dirty) {
    if (!is_valid_yomi(yomi)) {
        LOG_WARNING("invalid candidate: \"%s\"", to_utf8(yomi).c_str());
        return;
    }
    std::vector<ustring> words;
    for (const auto& w : words_in) {
        if (w.empty() || w == yomi || has(words, w)) continue;
        words.push_back(w);
    }
    if (words.empty()) return;

    ustring katakana;
    if (last(yomi) != DASH) {
        katakana = to_katakana(yomi);
        if (!has(words, katakana)) katakana.clear();
    }

    const std::vector<ustring>* existing = dic.get(yomi);
    if (!existing) {
        if (!reorder_only) {
            dic.put(yomi, words);
        } else if (!katakana.empty()) {
            dic.put(yomi, {katakana});
        } else {
            dirty = true;
        }
        index_t size = len(yomi);
        if (last(yomi) == DASH) --size;
        max_len = std::max(size, max_len);
    } else {
        std::vector<ustring> update = *existing;
        std::vector<ustring> yougen;
        for (auto w = words.rbegin(); w != words.rend(); ++w) {
            const ustring& word = *w;
            if (has(update, word)) {
                remove_first(update, word);
                update.insert(update.begin(), word);
            } else if (!reorder_only || is_hiragana(word[0]) || word[0] == U'#' || word == katakana) {
                if (last(word) == DASH) {
                    yougen.insert(yougen.begin(), word);
                } else {
                    update.insert(update.begin(), word);
                }
            } else {
                dirty = true;
            }
        }
        update.insert(update.end(), yougen.begin(), yougen.end());
        dic.put(yomi, std::move(update));
    }
}

void Dictionary::remove_entries(Table& dic, const ustring& yomi, const std::vector<ustring>& cand) {
    const std::vector<ustring>* existing = dic.get(yomi);
    if (!existing) return;
    std::vector<ustring> update;
    for (const auto& x : *existing) {
        if (!has(cand, x)) update.push_back(x);
    }
    if (!update.empty())
        dic.put(yomi, std::move(update));
    else
        dic.erase(yomi);
}

void Dictionary::set_entry(const ustring& yomi, const std::vector<ustring>& cand) {
    dict_.put(yomi, cand);
    touched_.insert(yomi);
}

void Dictionary::add_katakana(const ustring& word) {
    LOG_DEBUG("add_katakana(\"%s\")", to_utf8(word).c_str());
    ustring yomi = to_hiragana(word);
    std::vector<ustring> words;
    if (auto found = dict_.get(yomi)) words = *found;
    remove_first(words, word);
    words.insert(words.begin(), word);
    set_entry(yomi, words);
    dirty_ = true;
}

void Dictionary::reset() {
    if (!shrunk_.empty()) {
        if (auto found = dict_.get(yomi_)) {
            if (has(*found, shrunk_)) {
                std::vector<ustring> words = *found;
                remove_first(words, shrunk_);
                dict_.put(yomi_, words);
            }
        }
        shrunk_.clear();
    }
    yomi_.clear();
    numeric_.clear();
}

std::pair<ustring, ustring> Dictionary::get_stem(index_t no) const {
    ustring yomi = yomi_;
    if (yomi.empty()) return {ustring(), ustring()};
    if (!order_.empty()) {
        yomi = slice_to(yomi, static_cast<index_t>(yomi.find(DASH)) + 1);
        if (no < 0 || static_cast<index_t>(order_.size()) <= no) return {yomi, ustring()};
        no = order_[no];
        auto found = dict_.get(yomi);
        if (!found || static_cast<index_t>(found->size()) <= no) return {yomi, ustring()};
        return {yomi, (*found)[no]};
    }
    if (!numeric_.empty()) {
        yomi = U"#" + yomi;
        auto found = dict_.get(yomi);
        if (!found || static_cast<index_t>(found->size()) <= no) {
            if (no < 0 || static_cast<index_t>(cand_.size()) <= no) return {yomi, ustring()};
            return {yomi, U"#" + cand_[no]};
        }
        return {yomi, (*found)[no]};
    }
    if (no < 0 || static_cast<index_t>(cand_.size()) <= no) return {yomi, ustring()};
    return {yomi, cand_[no]};
}

std::vector<ustring> Dictionary::get_stem_list() const {
    std::vector<ustring> stem_list;
    for (index_t i = 0; i < static_cast<index_t>(cand_.size()); ++i) stem_list.push_back(get_stem(i).second);
    return stem_list;
}

bool Dictionary::is_rejected(const ustring& yomi, const ustring& word) const {
    auto found = rejected_.find(yomi);
    return found != rejected_.end() && found->second.count(word) != 0;
}

void Dictionary::reject(const ustring& yomi, const ustring& word) { rejected_[yomi].insert(word); }

void Dictionary::accept(const ustring& yomi, const ustring& word) {
    auto found = rejected_.find(yomi);
    if (found != rejected_.end()) found->second.erase(word);
}

void Dictionary::remove_entry(const ustring& yomi, const ustring& word) {
    auto found = dict_.get(yomi);
    if (!found || !has(*found, word)) return;
    std::vector<ustring> cand = *found;
    remove_first(cand, word);
    if (cand.empty()) {
        dict_.erase(yomi);
    } else {
        dict_.put(yomi, cand);
    }
}

ustring Dictionary::set_current_to_next() {
    if (no_ + 1 < static_cast<index_t>(cand_.size())) ++no_;
    return cand_[no_];
}

ustring Dictionary::set_current_to_previous() {
    if (0 < no_) --no_;
    return cand_[no_];
}

ustring Dictionary::current() const {
    if (!yomi_.empty()) return cand_[no_];
    return ustring();
}

void Dictionary::set_current(index_t index) {
    if (!yomi_.empty() && 0 <= index && index < static_cast<index_t>(cand_.size())) no_ = index;
}

std::vector<ustring> Dictionary::cand() const {
    if (!yomi_.empty()) return cand_;
    return {};
}

const std::vector<ustring>* Dictionary::entry(const ustring& yomi) const {
    return dict_.get(yomi);
}

// -1: yomi is not valid
// 0: yomi can be valid; still missing a few letters
// 1: yomi is valid
int Dictionary::match(const ustring& okuri_in, const ustring& yomi_in) const {
    ustring okuri = okuri_in;
    ustring yomi = yomi_in;
    char32_t suffix = 0;
    if (!okuri.empty() && contains(SUFFIX_CODES, okuri.back())) {
        suffix = okuri.back();
        okuri.pop_back();
    }
    index_t pos = std::min(len(okuri), len(yomi));

    // Check the fixed part of Okurigana
    if (!okuri.empty()) {
        if (slice_to(yomi, pos) == slice_to(okuri, pos)) {
            if (pos == len(okuri)) {
                if (!suffix) return len(okuri) < len(yomi) ? 1 : 0;
            } else {
                return 0;
            }
        } else if (len(okuri) < len(yomi)) {
            return -1;
        } else if (compare_okuri(okuri, yomi)) {
            return 0;
        } else {
            return -1;
        }
    }

    const auto& table = katuyou_table();
    auto found = table.find(suffix);
    if (!suffix || found == table.end()) return -1;

    // Check conjugations
    yomi = slice_from(yomi, pos);
    if (yomi.empty()) return 0;
    const Katuyou& katuyou = found->second;
    index_t conj_len = 0;
    bool has_empty = false;
    for (const auto& k : katuyou) {
        if (!k) continue;
        conj_len = std::max(conj_len, len(*k));
        if (k->empty()) has_empty = true;
    }
    for (index_t i = std::min(len(yomi), conj_len); 0 < i; --i) {
        ustring head = slice_to(yomi, i);
        for (const auto& k : katuyou) {
            if (!k || len(*k) <= i) continue;
            if (len(yomi) < len(*k) && (slice_to(*k, i) == head || compare_okuri(slice_to(*k, i), head))) return 0;
        }
        for (const auto& k : katuyou) {
            if (!k || len(*k) != i) continue;
            if (*k == head) {
                return i < len(yomi) ? 1 : 0;
            } else if (len(yomi) == len(*k) && compare_okuri(*k, head)) {
                return 0;
            }
        }
    }

    // cf. 食べ
    if (has_empty) return 0 < len(yomi) ? 1 : 0;

    return -1;
}

bool Dictionary::lookup_next_taigen(const ustring& text, index_t start, index_t pos) {
    // Return false if no more lookup is necessary.
    if (len(text) <= start || !is_hiragana(text[start])) return false;
    ustring yomi = slice(text, start, pos);
    if (auto found = dict_.get(yomi)) {
        yomi_ = yomi;
        cand_ = *found;
        no_ = 0;
        order_.clear();
        completed_.clear();
        numeric_.clear();
    }
    return true;
}

void Dictionary::lookup_numeric(const ustring& text, index_t start, index_t pos, const ustring& numeric) {
    ustring yomi = replace_all(slice(text, start, pos), numeric, U"#");
    const std::vector<ustring>* found = dict_.get(yomi);
    if (!found) return;
    std::vector<ustring> cand;
    if (slice_from(yomi, 1) == yomi_) {
        cand = cand_;
    } else {
        yomi_ = slice_from(yomi, 1);
    }
    const std::vector<ustring> words = *found;
    for (auto w = words.rbegin(); w != words.rend(); ++w) {
        ustring word = slice_from(*w, 1);
        remove_first(cand, word);
        cand.insert(cand.begin(), word);
    }
    cand_ = cand;
    no_ = 0;
    order_.clear();
    completed_.clear();
    numeric_ = numeric;
}

bool Dictionary::lookup_next_yougen(const ustring& text, index_t start, index_t pos, index_t suffix) {
    // Return false if no more lookup is necessary.
    index_t end = suffix + 1;
    if (len(text) <= start || !is_hiragana(text[start])) return false;
    const std::vector<ustring>* found = dict_.get(slice(text, start, end));
    if (!found) return true;

    std::vector<ustring> cand[2];
    std::vector<index_t> order[2];
    const ustring rest = slice_from(text, end);
    const ustring typed = slice(text, end, pos);
    index_t n = 0;
    for (const ustring& entry : *found) {
        index_t pos_okuri = suffix_start(entry);
        ustring okuri = slice_from(entry, pos_okuri);
        int p = match(okuri, rest);
        ustring word = slice_to(entry, pos_okuri) + typed;
        if (0 <= p && !has(cand[p], word)) {
            cand[p].push_back(word);
            order[p].push_back(n);
        }
        ++n;
    }
    if (!cand[0].empty() || !cand[1].empty()) {
        for (const auto& word : cand[0]) {
            auto at = std::find(cand[1].begin(), cand[1].end(), word);
            if (at != cand[1].end()) {
                order[1].erase(order[1].begin() + (at - cand[1].begin()));
                cand[1].erase(at);
            }
        }
        yomi_ = slice(text, start, pos);
        cand_ = cand[0];
        cand_.insert(cand_.end(), cand[1].begin(), cand[1].end());
        no_ = 0;
        order_ = order[0];
        order_.insert(order_.end(), order[1].begin(), order[1].end());
        completed_.assign(cand[0].size(), 0);
        completed_.insert(completed_.end(), cand[1].size(), 1);
        numeric_.clear();
    }
    return true;
}

ustring Dictionary::lookup(const ustring& text_in, index_t pos, index_t anchor) {
    ustring text = text_in;
    if (anchor + max_len_ < pos) anchor = pos - max_len_;
    reset();
    index_t suffix = rfind(slice(text, anchor, pos), ustring(1, DASH));
    if (0 <= suffix) {
        suffix += anchor;
        // OKURI = ―[HIRAGANA]* always matches here.
        index_t end = suffix + 1;
        while (end < len(text) && is_hiragana(text[end])) ++end;
        text = slice_to(text, end);
    }
    if (suffix <= 0) {
        ustring numeric;
        for (index_t i = pos - 1; anchor <= i; --i) {
            if (len(text) <= i) break;
            if (is_numeric(text[i])) {
                numeric = text[i] + numeric;
                if (anchor < i && is_numeric(text[i - 1])) continue;
                lookup_numeric(text, i, pos, numeric);
                break;
            }
            if (!lookup_next_taigen(text, i, pos)) break;
        }
    } else {
        for (index_t i = suffix - 1; anchor <= i; --i) {
            if (!lookup_next_yougen(text, i, pos, suffix)) break;
        }
    }
    return current();
}

// Port of dictionary.py assisted_lookup(): looks the reading up and asks the
// language model which candidate fits the surrounding text.  A reading may be
// "shrunk" so that only a part of it is converted (e.g. 生きがい論 from がい論).
ustring Dictionary::assisted_lookup(Model& model, const ustring& text_in, index_t pos, index_t anchor,
                                    index_t& suggested_out) {
    // Clear the shrunk word of the previous lookup.
    reset();

    if (anchor + max_len_ < pos) anchor = pos - max_len_;
    reset();
    index_t suggested = 0;
    ustring suggested_word;
    ustring shrunk;
    ustring yomi;
    ustring text = text_in;
    index_t suffix = rfind(slice(text, anchor, pos), ustring(1, DASH));
    if (0 <= suffix) {
        suffix += anchor;
        // OKURI = ―[HIRAGANA]* always matches here.
        index_t end = suffix + 1;
        while (end < len(text) && is_hiragana(text[end])) ++end;
        text = slice_to(text, end);
    }
    if (suffix <= 0) {
        // Taigen (nouns, numbers) or numeric conversion.
        ustring numeric;
        for (index_t i = pos - 1; anchor <= i; --i) {
            if (len(text) <= i) break;
            if (is_numeric(text[i])) {
                numeric = text[i] + numeric;
                if (anchor < i && is_numeric(text[i - 1])) continue;
                lookup_numeric(text, i, pos, numeric);
                if (!numeric_.empty()) {
                    auto p_dict = model.assist(slice_to(text, pos - len(yomi_)), yomi_, cand_);
                    shrunk.clear();
                    suggested = argmax(p_dict);
                }
                break;
            }
            bool cont = lookup_next_taigen(text, i, pos);
            if (yomi != yomi_) {
                if (!suggested_word.empty()) {
                    // self._yomi[:-len(yomi)]; with an empty yomi Python yields ''.
                    ustring head = yomi.empty() ? ustring() : slice_to(yomi_, len(yomi_) - len(yomi));
                    suggested_word = head + suggested_word;
                    if (!has(cand_, suggested_word)) {
                        std::vector<ustring> cand = cand_;
                        cand.insert(cand.begin(), suggested_word);
                        cand_ = cand;
                        shrunk = suggested_word;
                    } else {
                        shrunk.clear();
                    }
                }
                yomi = yomi_;
                auto p_dict = model.assist(slice_to(text, pos - len(yomi_)), yomi_, cand_);
                suggested = argmax(p_dict);
                if (!cand_.empty()) suggested_word = cand_[suggested];
                auto first = p_dict.find(0);
                double p_first = first == p_dict.end() ? 0.0 : first->second;
                if (!shrunk.empty() && !cand_.empty() &&
                    (p_first < static_cast<double>(suggested) || is_rejected(yomi, shrunk))) {
                    cand_.erase(cand_.begin());
                    shrunk.clear();
                    --suggested;
                    if (suggested < 0) {
                        p_dict.erase(0);
                        suggested = argmax(p_dict) - 1;
                        if (0 <= suggested && suggested < static_cast<index_t>(cand_.size()))
                            suggested_word = cand_[suggested];
                    }
                }
            }
            if (!cont) break;
        }
        if (!shrunk.empty()) {
            // Temporarily register the shrunk word in the working dictionary.
            if (auto found = dict_.get(yomi)) {
                if (!has(*found, shrunk)) {
                    std::vector<ustring> cand = *found;
                    cand.insert(cand.begin(), shrunk);
                    dict_.put(yomi, cand);
                    shrunk_ = shrunk;
                }
            }
        }
    } else {
        // Yougen (inflected words).
        std::vector<ustring> cand;
        ustring stem;
        index_t pos_found = -1;
        index_t pos_shrunk = -1;
        for (index_t i = suffix - 1; anchor <= i; --i) {
            if (len(text) <= i || !is_hiragana(text[i])) break;
            if (!stem.empty()) {
                const std::vector<ustring>* found = dict_.get(slice(text, i, suffix + 1));
                if (!found) continue;
                cand = *found;
                if (!shrunk.empty()) {
                    // Remove the shrunk word created by the previous lookup.
                    ustring key = slice(text, pos_shrunk, suffix + 1);
                    if (auto prev = dict_.get(key)) {
                        if (has(*prev, shrunk)) {
                            std::vector<ustring> words = *prev;
                            remove_first(words, shrunk);
                            dict_.put(key, words);
                        }
                    }
                }
                shrunk = slice(text, i, pos_found) + stem;
                pos_shrunk = i;
                ustring key = slice(text, i, suffix + 1);
                if (!has(cand, shrunk)) {
                    // Temporarily register the shrunk word in the working dictionary.
                    std::vector<ustring> words = cand;
                    words.insert(words.begin(), shrunk);
                    dict_.put(key, words);
                } else {
                    shrunk.clear();
                }
            }
            bool cont = lookup_next_yougen(text, i, pos, suffix);
            if (yomi != yomi_) {
                yomi = yomi_;
                pos_found = i;
                std::vector<ustring> stem_list = get_stem_list();
                auto p_dict = model.assist_yougen(slice_to(text, pos - len(yomi)), yomi, stem_list);
                suggested = argmax(p_dict);
                // Look for the shrunk word in cand_.
                std::pair<ustring, ustring> stem_pair = get_stem(suggested);
                stem = stem_pair.second;
                if (!shrunk.empty() && (stem != shrunk || is_rejected(stem_pair.first, shrunk))) {
                    for (index_t j = 0; j < static_cast<index_t>(cand_.size()); ++j) {
                        std::pair<ustring, ustring> pair = get_stem(j);
                        if (pair.second == shrunk) {
                            cand_.erase(cand_.begin() + j);
                            order_.erase(order_.begin() + j);
                            completed_.erase(completed_.begin() + j);
                            --suggested;
                            if (suggested < 0) {
                                p_dict.erase(0);
                                suggested = argmax(p_dict) - 1;
                            }
                            break;
                        }
                    }
                    // Do not clear shrunk; it is still in dict_.
                }
            }
            if (!cont) break;
        }
        if (0 <= suggested && suggested < static_cast<index_t>(cand_.size())) {
            LOG_DEBUG("assisted_lookup: %s \"%s-%s\"", to_utf8(yomi_).c_str(), to_utf8(shrunk).c_str(),
                      to_utf8(cand_[suggested]).c_str());
        }
        shrunk_ = shrunk;
    }

    suggested_out = suggested;
    return current();
}

bool Dictionary::is_complete() const {
    // Return true if the current yougen conversion is completed.
    ustring cur = current();
    if (cur.empty()) return false;
    if (cur.find(DASH) != ustring::npos) return false;
    if (yomi_.find(DASH) == ustring::npos) return false;
    return !has(completed_, 0);
}

index_t Dictionary::confirm(const ustring& shrunk) {
    if (yomi_.empty()) return 0;

    index_t no_orig = no_;
    ustring yomi = yomi_;
    index_t no = no_;
    std::vector<ustring> cand;
    bool own = false;  // true if cand is self._cand in dictionary.py

    if (!order_.empty()) {
        yomi = slice_to(yomi, static_cast<index_t>(yomi.find(DASH)) + 1);
        no = order_[no];
        if (auto found = dict_.get(yomi)) cand = *found;
    } else if (!numeric_.empty()) {
        yomi = U"#" + yomi;
        if (auto found = dict_.get(yomi)) cand = *found;
        if (static_cast<index_t>(cand.size()) <= no) {
            cand.push_back(U"#" + cand_[no]);
            no = static_cast<index_t>(cand.size()) - 1;
        }
    } else {
        cand = cand_;
        own = true;
    }

    if (no < 0 || static_cast<index_t>(cand.size()) <= no) return 0;

    // Update the order of the candidates.
    ustring first = cand[no];
    if (first == yomi) {
        // Ignore pseudo candidates
        return 0;
    }
    if (0 < no) {
        remove_first(cand, first);
        cand.insert(cand.begin(), first);
        set_entry(yomi, cand);
        if (own) cand_ = cand;
        dirty_ = true;
    }

    // Personalize the dictionary if the candidate has been selected by shrinking the reading.
    if (!shrunk.empty() && numeric_.empty()) {
        yomi = shrunk + yomi;
        const std::vector<ustring>* found = dict_.get(yomi);
        if (found) {
            first = shrunk + first;
            std::vector<ustring> c = *found;
            remove_first(c, first);
            c.insert(c.begin(), first);
            set_entry(yomi, c);
            dirty_ = true;
        } else if (last(yomi) == DASH) {
            if (dict_.get(slice_to(yomi, len(yomi) - 1))) {
                first = shrunk + first;
                set_entry(yomi, {first});
                dirty_ = true;
            }
        }
    }

    return no_orig;
}

void Dictionary::create_pseudo_candidate(const ustring& text) {
    yomi_ = text;
    cand_ = {text};
    order_ = {0};
    completed_ = {0};
    no_ = 0;
    numeric_.clear();
}

bool Dictionary::is_pseudo_candidate() const {
    return !yomi_.empty() && has(cand_, yomi_);
}

fs::file_time_type Dictionary::history_time() const {
    std::error_code ec;
    auto t = fs::last_write_time(orders_path_, ec);
    return ec ? fs::file_time_type{} : t;
}

void Dictionary::reload_history() {
    Dict mine;
    for (const auto& yomi : touched_) {
        auto it = dict_.own.find(yomi);
        if (it != dict_.own.end()) mine[yomi] = it->second;
    }
    dict_.own.clear();
    max_len_ = base_->max_len;
    load_dict(dict_, orders_path_, max_len_, dirty_, false);
    for (auto& [yomi, words] : mine) dict_.own[yomi] = words;
    orders_time_ = history_time();
}

void Dictionary::sync() {
    if (history_time() != orders_time_) {
        LOG_INFO("input history changed by another process; reloading");
        reload_history();
    }
}

void Dictionary::write_orders(const fs::path& filename) {
    std::ofstream f(filename, std::ios::binary | std::ios::trunc);
    if (!f) throw fs::filesystem_error("could not open", filename, std::make_error_code(std::errc::io_error));
    f << "; " << kDictionaryVersion << "\n";
    // Only the entries changed from the base dictionaries are written.
    std::map<ustring, const std::vector<ustring>*> sorted;
    for (const auto& [yomi, words] : dict_.own) {
        if (!words.empty()) sorted[yomi] = &words;
    }
    for (const auto& [yomi, words] : sorted) {
        auto base = base_->dict.find(yomi);
        if (base != base_->dict.end() && base->second == *words) continue;
        ustring line = yomi + U" /";
        for (size_t i = 0; i < words->size(); ++i) {
            if (i) line += U'/';
            line += (*words)[i];
        }
        line += U"/\n";
        f << to_utf8(line);
    }
    f.close();
    if (!f) throw fs::filesystem_error("could not write", filename, std::make_error_code(std::errc::io_error));
}

void Dictionary::save_orders() {
    if (!dirty_) return;
    try {
        // Keep the entries other processes have saved meanwhile.
        if (history_time() != orders_time_) reload_history();
        if (!fs::exists(orders_path_)) {
            write_orders(orders_path_);
        } else {
            std::random_device rd;
            fs::path bakfile = orders_path_;
            bakfile += ".bak";
            fs::path tmpfile = orders_path_;
            tmpfile += "." + std::to_string(rd()) + ".tmp";
            write_orders(tmpfile);
            std::error_code ec;
            fs::remove(bakfile, ec);
            fs::copy_file(orders_path_, bakfile, fs::copy_options::overwrite_existing, ec);
            fs::rename(tmpfile, orders_path_);
        }
        orders_time_ = history_time();
        touched_.clear();
    } catch (const std::exception& e) {
        LOG_ERROR("could not save the orders in \"%s\": %s", orders_path_.u8string().c_str(), e.what());
    }
    dirty_ = false;
}

}  // namespace hiragana
