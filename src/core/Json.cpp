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

#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hiragana::json {

namespace {

const std::string kEmptyString;
const Value::Array kEmptyArray;
const Value::Object kEmptyObject;
const Value kNull;

void append_utf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {
        // Skip a UTF-8 BOM.
        if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) i_ = 3;
    }

    Value parse_document() {
        Value v = parse_value();
        skip_ws();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) {
        throw ParseError(std::string(what) + " at offset " + std::to_string(i_));
    }

    void skip_ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool consume(const char* literal) {
        size_t n = std::char_traits<char>::length(literal);
        if (s_.compare(i_, n, literal) == 0) {
            i_ += n;
            return true;
        }
        return false;
    }

    Value parse_value() {
        skip_ws();
        if (i_ >= s_.size()) fail("unexpected end");
        char c = s_[i_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Value(parse_string());
        if (consume("true")) return Value(true);
        if (consume("false")) return Value(false);
        if (consume("null")) return Value();
        if (c == '-' || ('0' <= c && c <= '9')) return parse_number();
        fail("unexpected character");
    }

    Value parse_object() {
        ++i_;
        Value::Object obj;
        skip_ws();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            return Value(std::move(obj));
        }
        for (;;) {
            skip_ws();
            if (i_ >= s_.size() || s_[i_] != '"') fail("expected key");
            std::string key = parse_string();
            skip_ws();
            if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
            ++i_;
            obj[key] = parse_value();
            skip_ws();
            if (i_ < s_.size() && s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return Value(std::move(obj));
            }
            fail("expected ',' or '}'");
        }
    }

    Value parse_array() {
        ++i_;
        Value::Array arr;
        skip_ws();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            return Value(std::move(arr));
        }
        for (;;) {
            arr.push_back(parse_value());
            skip_ws();
            if (i_ < s_.size() && s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return Value(std::move(arr));
            }
            fail("expected ',' or ']'");
        }
    }

    unsigned long parse_hex4() {
        if (i_ + 4 > s_.size()) fail("bad \\u escape");
        unsigned long v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if ('0' <= c && c <= '9') v |= c - '0';
            else if ('a' <= c && c <= 'f') v |= c - 'a' + 10;
            else if ('A' <= c && c <= 'F') v |= c - 'A' + 10;
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string parse_string() {
        ++i_;  // opening quote
        std::string out;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) break;
            char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned long cp = parse_hex4();
                    if (0xD800 <= cp && cp <= 0xDBFF && s_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        unsigned long low = parse_hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
        fail("unterminated string");
    }

    Value parse_number() {
        size_t start = i_;
        if (s_[i_] == '-') ++i_;
        while (i_ < s_.size() && (isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' || s_[i_] == 'e' ||
                                  s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-'))
            ++i_;
        return Value(std::strtod(s_.substr(start, i_ - start).c_str(), nullptr));
    }

    const std::string& s_;
    size_t i_ = 0;
};

void dump_string(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

}  // namespace

const std::string& Value::as_string() const { return is_string() ? string_ : kEmptyString; }
const Value::Array& Value::as_array() const { return is_array() ? *array_ : kEmptyArray; }
const Value::Object& Value::as_object() const { return is_object() ? *object_ : kEmptyObject; }

bool Value::contains(const std::string& key) const {
    return is_object() && object_->count(key) != 0;
}

const Value& Value::operator[](const std::string& key) const {
    if (!is_object()) return kNull;
    auto it = object_->find(key);
    return it == object_->end() ? kNull : it->second;
}

void Value::set(const std::string& key, Value value) {
    if (!is_object()) {
        *this = Value(Object{});
    } else if (object_.use_count() > 1) {
        object_ = std::make_shared<Object>(*object_);
    }
    (*object_)[key] = std::move(value);
}

std::string Value::dump(int indent) const {
    std::string out;
    dump_to(out, indent, 0);
    out += '\n';
    return out;
}

void Value::dump_to(std::string& out, int indent, int depth) const {
    auto newline = [&](int d) {
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[32];
            if (std::floor(number_) == number_ && std::fabs(number_) < 1e15)
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(number_));
            else
                std::snprintf(buf, sizeof buf, "%.17g", number_);
            out += buf;
            break;
        }
        case Type::String: dump_string(out, string_); break;
        case Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& v : *array_) {
                if (!first) out += ',';
                first = false;
                newline(depth + 1);
                v.dump_to(out, indent, depth + 1);
            }
            if (!array_->empty()) newline(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [k, v] : *object_) {
                if (!first) out += ',';
                first = false;
                newline(depth + 1);
                dump_string(out, k);
                out += ": ";
                v.dump_to(out, indent, depth + 1);
            }
            if (!object_->empty()) newline(depth);
            out += '}';
            break;
        }
    }
}

Value parse(const std::string& text) {
    return Parser(text).parse_document();
}

}  // namespace hiragana::json
