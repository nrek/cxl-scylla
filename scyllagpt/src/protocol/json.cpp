#include "scyllagpt/json.h"

#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace scyllagpt {
namespace {

const Json kNull;
const std::string kEmpty;

void skip_ws(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
        ++i;
    }
}

bool parse_value(std::string_view s, std::size_t& i, Json& out, std::string* err);

bool hex4(std::string_view s, std::size_t i, unsigned& cp) {
    if (i + 4 > s.size()) {
        return false;
    }
    cp = 0;
    for (int k = 0; k < 4; ++k) {
        const char c = s[i + k];
        unsigned v = 0;
        if (c >= '0' && c <= '9') {
            v = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v = 10 + (c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            v = 10 + (c - 'A');
        } else {
            return false;
        }
        cp = (cp << 4) | v;
    }
    return true;
}

void append_utf8(std::string& o, unsigned cp) {
    if (cp <= 0x7F) {
        o.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool parse_string(std::string_view s, std::size_t& i, std::string& out, std::string* err) {
    if (i >= s.size() || s[i] != '"') {
        if (err) {
            *err = "expected string";
        }
        return false;
    }
    ++i;
    out.clear();
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            ++i;
            return true;
        }
        if (c == '\\') {
            ++i;
            if (i >= s.size()) {
                if (err) {
                    *err = "unterminated escape";
                }
                return false;
            }
            const char e = s[i++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(s, i, cp)) {
                        if (err) {
                            *err = "bad unicode escape";
                        }
                        return false;
                    }
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            unsigned lo = 0;
                            if (!hex4(s, i + 2, lo) || lo < 0xDC00 || lo > 0xDFFF) {
                                if (err) {
                                    *err = "bad surrogate pair";
                                }
                                return false;
                            }
                            cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                            i += 6;
                        }
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    if (err) {
                        *err = "bad escape";
                    }
                    return false;
            }
            continue;
        }
        if (c < 0x20) {
            if (err) {
                *err = "unescaped control";
            }
            return false;
        }
        out.push_back(static_cast<char>(c));
        ++i;
    }
    if (err) {
        *err = "unterminated string";
    }
    return false;
}

bool parse_number(std::string_view s, std::size_t& i, Json& out, std::string* err) {
    const std::size_t start = i;
    if (i < s.size() && s[i] == '-') {
        ++i;
    }
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
        if (err) {
            *err = "bad number";
        }
        return false;
    }
    if (s[i] == '0') {
        ++i;
    } else {
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
            if (err) {
                *err = "bad number fraction";
            }
            return false;
        }
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            ++i;
        }
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
            if (err) {
                *err = "bad exponent";
            }
            return false;
        }
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
    }
    out = Json::number_raw(std::string(s.substr(start, i - start)));
    return true;
}

bool parse_array(std::string_view s, std::size_t& i, Json& out, std::string* err) {
    if (i >= s.size() || s[i] != '[') {
        return false;
    }
    ++i;
    out = Json::array();
    skip_ws(s, i);
    if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
    }
    while (true) {
        Json v;
        if (!parse_value(s, i, v, err)) {
            return false;
        }
        out.push(std::move(v));
        skip_ws(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            skip_ws(s, i);
            continue;
        }
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        if (err) {
            *err = "expected comma or ]";
        }
        return false;
    }
}

bool parse_object(std::string_view s, std::size_t& i, Json& out, std::string* err) {
    if (i >= s.size() || s[i] != '{') {
        return false;
    }
    ++i;
    out = Json::object();
    skip_ws(s, i);
    if (i < s.size() && s[i] == '}') {
        ++i;
        return true;
    }
    while (true) {
        skip_ws(s, i);
        std::string key;
        if (!parse_string(s, i, key, err)) {
            return false;
        }
        skip_ws(s, i);
        if (i >= s.size() || s[i] != ':') {
            if (err) {
                *err = "expected colon";
            }
            return false;
        }
        ++i;
        Json v;
        if (!parse_value(s, i, v, err)) {
            return false;
        }
        out[key] = std::move(v);
        skip_ws(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            continue;
        }
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        if (err) {
            *err = "expected comma or }";
        }
        return false;
    }
}

bool parse_value(std::string_view s, std::size_t& i, Json& out, std::string* err) {
    skip_ws(s, i);
    if (i >= s.size()) {
        if (err) {
            *err = "unexpected end";
        }
        return false;
    }
    const char c = s[i];
    if (c == 'n') {
        if (s.substr(i, 4) != "null") {
            if (err) {
                *err = "expected null";
            }
            return false;
        }
        i += 4;
        out = Json::null();
        return true;
    }
    if (c == 't') {
        if (s.substr(i, 4) != "true") {
            if (err) {
                *err = "expected true";
            }
            return false;
        }
        i += 4;
        out = Json::boolean(true);
        return true;
    }
    if (c == 'f') {
        if (s.substr(i, 5) != "false") {
            if (err) {
                *err = "expected false";
            }
            return false;
        }
        i += 5;
        out = Json::boolean(false);
        return true;
    }
    if (c == '"') {
        std::string str;
        if (!parse_string(s, i, str, err)) {
            return false;
        }
        out = Json::string(std::move(str));
        return true;
    }
    if (c == '[') {
        return parse_array(s, i, out, err);
    }
    if (c == '{') {
        return parse_object(s, i, out, err);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        return parse_number(s, i, out, err);
    }
    if (err) {
        *err = "unexpected token";
    }
    return false;
}

void dump_into(const Json& j, std::string& o) {
    switch (j.type()) {
        case Json::Type::Null:
            o += "null";
            return;
        case Json::Type::Bool:
            o += j.as_bool() ? "true" : "false";
            return;
        case Json::Type::Number:
            o += j.as_string();
            return;
        case Json::Type::String:
            o += json_escape(j.as_string());
            return;
        case Json::Type::Array: {
            o += '[';
            bool first = true;
            for (const auto& v : j.array_items()) {
                if (!first) {
                    o += ',';
                }
                first = false;
                dump_into(v, o);
            }
            o += ']';
            return;
        }
        case Json::Type::Object: {
            o += '{';
            bool first = true;
            for (const auto& [k, v] : j.object_items()) {
                if (!first) {
                    o += ',';
                }
                first = false;
                o += json_escape(k);
                o += ':';
                dump_into(v, o);
            }
            o += '}';
            return;
        }
    }
}

}  // namespace

Json Json::null() {
    return {};
}

Json Json::boolean(bool v) {
    Json j;
    j.type_ = Type::Bool;
    j.bool_ = v;
    return j;
}

Json Json::number(std::int64_t v) {
    Json j;
    j.type_ = Type::Number;
    j.number_ = std::to_string(v);
    return j;
}

Json Json::number_raw(std::string raw) {
    Json j;
    j.type_ = Type::Number;
    j.number_ = std::move(raw);
    return j;
}

Json Json::string(std::string v) {
    Json j;
    j.type_ = Type::String;
    j.string_ = std::move(v);
    return j;
}

Json Json::array() {
    Json j;
    j.type_ = Type::Array;
    return j;
}

Json Json::object() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

Json Json::parse(std::string_view text, std::string* error) {
    std::size_t i = 0;
    Json out;
    if (!parse_value(text, i, out, error)) {
        return Json::null();
    }
    skip_ws(text, i);
    if (i != text.size()) {
        if (error) {
            *error = "trailing data";
        }
        return Json::null();
    }
    if (error) {
        error->clear();
    }
    return out;
}

bool Json::as_bool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

std::int64_t Json::as_int(std::int64_t fallback) const {
    if (type_ != Type::Number) {
        return fallback;
    }
    try {
        return std::stoll(number_);
    } catch (...) {
        return fallback;
    }
}

const std::string& Json::as_string() const {
    if (type_ == Type::String) {
        return string_;
    }
    if (type_ == Type::Number) {
        return number_;
    }
    return kEmpty;
}

std::string Json::as_string(const char* fallback) const {
    if (type_ == Type::String) {
        return string_;
    }
    if (type_ == Type::Number) {
        return number_;
    }
    if (type_ == Type::Bool) {
        return bool_ ? "true" : "false";
    }
    return fallback ? fallback : "";
}

bool Json::has(const char* key) const {
    return type_ == Type::Object && object_.find(key) != object_.end();
}

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        *this = object();
    }
    return object_[key];
}

const Json& Json::at(const std::string& key) const {
    if (type_ != Type::Object) {
        return kNull;
    }
    const auto it = object_.find(key);
    return it == object_.end() ? kNull : it->second;
}

Json& Json::operator[](std::size_t index) {
    if (type_ != Type::Array) {
        *this = array();
    }
    if (index >= array_.size()) {
        array_.resize(index + 1);
    }
    return array_[index];
}

const Json& Json::at(std::size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) {
        return kNull;
    }
    return array_[index];
}

std::size_t Json::size() const {
    if (type_ == Type::Array) {
        return array_.size();
    }
    if (type_ == Type::Object) {
        return object_.size();
    }
    if (type_ == Type::String) {
        return string_.size();
    }
    return 0;
}

void Json::push(Json v) {
    if (type_ != Type::Array) {
        *this = array();
    }
    array_.push_back(std::move(v));
}

std::string Json::dump() const {
    std::string o;
    dump_into(*this, o);
    return o;
}

std::string json_escape(std::string_view s) {
    std::string o;
    o.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                o += "\\\"";
                break;
            case '\\':
                o += "\\\\";
                break;
            case '\b':
                o += "\\b";
                break;
            case '\f':
                o += "\\f";
                break;
            case '\n':
                o += "\\n";
                break;
            case '\r':
                o += "\\r";
                break;
            case '\t':
                o += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    o.push_back('"');
    return o;
}

bool JsonlDecoder::feed(const char* data, std::size_t n, std::vector<std::string>& lines, std::string* error) {
    if (buf_.size() + n > kMaxBuffer) {
        if (error) {
            *error = "JSONL buffer overflow";
        }
        buf_.clear();
        return false;
    }
    buf_.append(data, n);
    std::size_t start = 0;
    for (std::size_t i = 0; i < buf_.size(); ++i) {
        if (buf_[i] == '\n') {
            std::string line = buf_.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                lines.push_back(std::move(line));
            }
            start = i + 1;
        }
    }
    buf_.erase(0, start);
    return true;
}

}  // namespace scyllagpt
