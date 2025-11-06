// MiniJson.hpp - tiny JSON value + builder + parser (C++17, header-only)
// - Supports: null, bool, number (double), string (UTF-8), array, object
// - Build: just include; no libs. For Windows/WinHTTP agents, perfect for small payloads.
// Notes:
// - Numbers parsed/stored as double. If you need int64, extend as needed.
// - String escape handling covers \" \\ \/ \b \f \n \r \t. \uXXXX is preserved as literal backslash-u sequence.
// - Pretty printing optional via dump(indent=2). Use dump() for compact.

#pragma once
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <map>
#include <cctype>
#include <stdexcept>
#include <sstream>

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    // Types
    struct Null {};
    using Value = std::variant<Null, bool, double, std::string, Array, Object>;

    // ctors
    Json() : v_(Null{}) {}
    Json(std::nullptr_t) : v_(Null{}) {}
    Json(bool b) : v_(b) {}
    Json(double d) : v_(d) {}
    Json(int i) : v_(static_cast<double>(i)) {}
    Json(const char* s) : v_(std::string(s)) {}
    Json(std::string s) : v_(std::move(s)) {}
    Json(Array a) : v_(std::move(a)) {}
    Json(Object o) : v_(std::move(o)) {}

    // Static helpers
    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    // Type checks
    bool is_null()   const { return std::holds_alternative<Null>(v_); }
    bool is_bool()   const { return std::holds_alternative<bool>(v_); }
    bool is_num()    const { return std::holds_alternative<double>(v_); }
    bool is_str()    const { return std::holds_alternative<std::string>(v_); }
    bool is_array()  const { return std::holds_alternative<Array>(v_); }
    bool is_object() const { return std::holds_alternative<Object>(v_); }

    // Accessors (throws on wrong type)
    bool& as_bool() { return std::get<bool>(v_); }
    double& as_num() { return std::get<double>(v_); }
    std::string& as_str() { return std::get<std::string>(v_); }
    Array& as_array() { return std::get<Array>(v_); }
    Object& as_object() { return std::get<Object>(v_); }

    const bool& as_bool()   const { return std::get<bool>(v_); }
    const double& as_num()    const { return std::get<double>(v_); }
    const std::string& as_str()    const { return std::get<std::string>(v_); }
    const Array& as_array()  const { return std::get<Array>(v_); }
    const Object& as_object() const { return std::get<Object>(v_); }

    // Object conveniences
    Json& operator[](const std::string& key) {
        if (!is_object()) v_ = Object{};
        return std::get<Object>(v_)[key];
    }
    const Json& at(const std::string& key) const { return std::get<Object>(v_).at(key); }
    bool contains(const std::string& key) const {
        if (!is_object()) return false;
        return std::get<Object>(v_).count(key) != 0;
    }

    // Array conveniences
    void push_back(const Json& j) {
        if (!is_array()) v_ = Array{};
        std::get<Array>(v_).push_back(j);
    }

    // Serialization
    std::string dump(int indent = -1) const {
        std::string out;
        dump_impl(out, indent, 0);
        return out;
    }

    // Parsing
    static Json parse(std::string_view s) {
        Parser p(s);
        Json j = p.parse_value();
        p.skip_ws();
        if (!p.eof()) throw std::runtime_error("JSON: trailing characters");
        return j;
    }

private:
    Value v_;

    static std::string escape(const std::string& s) {
        std::string o;
        o.reserve(s.size() + 4);
        for (unsigned char c : s) {
            switch (c) {
            case '\"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
            // control chars -> \u00XX
                if (c < 0x20) { 
                    static const char* hex = "0123456789ABCDEF";
                    o += "\\u00";
                    o += hex[(c >> 4) & 0xF];
                    o += hex[c & 0xF];
                }
                else {
                    o += static_cast<char>(c);
                }
            }
        }
        return o;
    }

    void dump_impl(std::string& out, int indent, int depth) const {
        auto ind = [&](int d) { if (indent >= 0) out.append(d * indent, ' '); };

        if (is_null()) { out += "null"; return; }
        if (is_bool()) { out += as_bool() ? "true" : "false"; return; }
        if (is_num()) {
            // avoids trailing .000000
            std::ostringstream oss; oss.precision(15); oss << as_num();
            out += oss.str(); return;
        }
        if (is_str()) { out += '\"'; out += escape(as_str()); out += '\"'; return; }

        if (is_array()) {
            const auto& a = as_array();
            out += '[';
            if (!a.empty()) {
                if (indent >= 0) out += '\n';
                for (size_t i = 0; i < a.size(); ++i) {
                    ind(depth + 1);
                    a[i].dump_impl(out, indent, depth + 1);
                    if (i + 1 < a.size()) out += ',';
                    if (indent >= 0) out += '\n';
                }
                ind(depth);
            }
            out += ']';
            return;
        }

        // object
        const auto& o = as_object();
        out += '{';
        if (!o.empty()) {
            if (indent >= 0) out += '\n';
            size_t i = 0;
            for (const auto& kv : o) {
                ind(depth + 1);
                out += '\"'; out += escape(kv.first); out += "\":";
                if (indent >= 0) out += ' ';
                kv.second.dump_impl(out, indent, depth + 1);
                if (++i < o.size()) out += ',';
                if (indent >= 0) out += '\n';
            }
            ind(depth);
        }
        out += '}';
    }

    //Minimal recursive-descent parser 
    struct Parser {
        std::string_view s;
        size_t i{ 0 };
        Parser(std::string_view sv) : s(sv) {}

        bool eof() const { return i >= s.size(); }
        char peek() const { return eof() ? '\0' : s[i]; }
        char get() { return eof() ? '\0' : s[i++]; }
        void skip_ws() { while (!eof() && std::isspace((unsigned char)peek())) ++i; }
        void expect(char c) {
            if (get() != c) throw std::runtime_error(std::string("JSON: expected '") + c + "'");
        }

        Json parse_value() {
            skip_ws();
            char c = peek();
            if (c == 'n') return parse_null();
            if (c == 't' || c == 'f') return parse_bool();
            if (c == '"') return parse_string();
            if (c == '-' || std::isdigit((unsigned char)c)) return parse_number();
            if (c == '{') return parse_object();
            if (c == '[') return parse_array();
            throw std::runtime_error("JSON: unexpected token");
        }

        Json parse_null() {
            expect('n'); expect('u'); expect('l'); expect('l'); return Json(nullptr);
        }
        Json parse_bool() {
            if (peek() == 't') { expect('t'); expect('r'); expect('u'); expect('e'); return Json(true); }
            expect('f'); expect('a'); expect('l'); expect('s'); expect('e'); return Json(false);
        }
        Json parse_number() {
            size_t start = i;
            if (peek() == '-') ++i;
            if (peek() == '0') { ++i; }
            else {
                if (!std::isdigit((unsigned char)peek())) throw std::runtime_error("JSON: bad number");
                while (std::isdigit((unsigned char)peek())) ++i;
            }
            if (peek() == '.') { ++i; if (!std::isdigit((unsigned char)peek())) throw std::runtime_error("JSON: bad number"); while (std::isdigit((unsigned char)peek())) ++i; }
            if (peek() == 'e' || peek() == 'E') { ++i; if (peek() == '+' || peek() == '-') ++i; if (!std::isdigit((unsigned char)peek())) throw std::runtime_error("JSON: bad number"); while (std::isdigit((unsigned char)peek())) ++i; }
            double d = std::stod(std::string(s.substr(start, i - start)));
            return Json(d);
        }
        Json parse_string() {
            expect('"');
            std::string out;
            while (!eof()) {
                char c = get();
                if (c == '"') break;
                if (c == '\\') {
                    char e = get();
                    switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        // Minimal: keep as literal \uXXXX without decoding.
                        out += "\\u";
                        for (int k = 0; k < 4; k++) { char h = get(); out += h; }
                        break;
                    }
                    default: throw std::runtime_error("JSON: bad escape");
                    }
                }
                else {
                    out += c;
                }
            }
            return Json(std::move(out));
        }
        Json parse_array() {
            expect('[');
            Json::Array arr;
            skip_ws();
            if (peek() == ']') { get(); return Json(arr); }
            while (true) {
                arr.push_back(parse_value());
                skip_ws();
                char c = get();
                if (c == ']') break;
                if (c != ',') throw std::runtime_error("JSON: expected ',' or ']'");
                skip_ws();
            }
            return Json(arr);
        }
        Json parse_object() {
            expect('{');
            Json::Object obj;
            skip_ws();
            if (peek() == '}') { get(); return Json(obj); }
            while (true) {
                skip_ws();
                if (peek() != '"') throw std::runtime_error("JSON: expected string key");
                std::string key = parse_string().as_str();
                skip_ws(); expect(':');
                Json val = parse_value();
                obj.emplace(std::move(key), std::move(val));
                skip_ws();
                char c = get();
                if (c == '}') break;
                if (c != ',') throw std::runtime_error("JSON: expected ',' or '}'");
                skip_ws();
            }
            return Json(obj);
        }
    };
};
