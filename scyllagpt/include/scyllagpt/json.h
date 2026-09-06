#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json null();
    static Json boolean(bool v);
    static Json number(std::int64_t v);
    static Json number_raw(std::string raw);
    static Json string(std::string v);
    static Json array();
    static Json object();

    static Json parse(std::string_view text, std::string* error = nullptr);

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool fallback = false) const;
    std::int64_t as_int(std::int64_t fallback = 0) const;
    const std::string& as_string() const;
    std::string as_string(const char* fallback) const;

    bool has(const char* key) const;
    Json& operator[](const std::string& key);
    const Json& at(const std::string& key) const;
    Json& operator[](std::size_t index);
    const Json& at(std::size_t index) const;
    std::size_t size() const;
    void push(Json v);

    const std::map<std::string, Json>& object_items() const { return object_; }
    const std::vector<Json>& array_items() const { return array_; }

    std::string dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    std::string number_;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;
};

class JsonlDecoder {
public:
    static constexpr std::size_t kMaxBuffer = 16 * 1024 * 1024;

    // Feed bytes from a pipe. Returns complete JSONL lines (without trailing newline).
    // Incomplete trailing data stays buffered. Returns false on overflow.
    bool feed(const char* data, std::size_t n, std::vector<std::string>& lines, std::string* error);

    void clear() { buf_.clear(); }

private:
    std::string buf_;
};

std::string json_escape(std::string_view s);

}  // namespace scyllagpt
