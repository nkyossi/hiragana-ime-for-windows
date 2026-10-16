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

// A small JSON reader and writer for the layout files and settings.json.

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace hiragana::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;  // keys are UTF-8

    Value() = default;
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double n) : type_(Type::Number), number_(n) {}
    Value(int n) : type_(Type::Number), number_(n) {}
    Value(long long n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), array_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : type_(Type::Object), object_(std::make_shared<Object>(std::move(o))) {}

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool fallback = false) const { return is_bool() ? bool_ : fallback; }
    double as_number(double fallback = 0) const { return is_number() ? number_ : fallback; }
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    bool contains(const std::string& key) const;
    // Returns a null value for a missing key.
    const Value& operator[](const std::string& key) const;
    void set(const std::string& key, Value value);

    std::string dump(int indent = 4) const;

private:
    void dump_to(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::shared_ptr<Array> array_;
    std::shared_ptr<Object> object_;
};

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Value parse(const std::string& text);

}  // namespace hiragana::json
