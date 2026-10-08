#pragma once
// Minimal, self-contained JSON library for NPC state persistence.
// No external dependencies — C++17 only.

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <charconv>
#include <iomanip>
#include <locale>

namespace npc::serial {

// ═══════════════════════════════════════════════════════════════════════
// JsonValue
// ═══════════════════════════════════════════════════════════════════════

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray  = std::vector<JsonValue>;

class JsonValue {
public:
    using Var = std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        JsonArray,
        JsonObject
    >;

    JsonValue()                    : v_(nullptr) {}
    JsonValue(std::nullptr_t)      : v_(nullptr) {}
    JsonValue(bool b)              : v_(b) {}
    JsonValue(int i)               : v_(static_cast<int64_t>(i)) {}
    JsonValue(unsigned u)          : v_(static_cast<int64_t>(u)) {}
    JsonValue(int64_t i)           : v_(i) {}
    JsonValue(uint64_t u)          : v_(static_cast<int64_t>(u)) {}
    JsonValue(double d)            : v_(d) {}
    JsonValue(float f)             : v_(static_cast<double>(f)) {}
    JsonValue(const char* s)       : v_(std::string(s)) {}
    JsonValue(std::string s)       : v_(std::move(s)) {}
    JsonValue(JsonArray  a)        : v_(std::move(a)) {}
    JsonValue(JsonObject o)        : v_(std::move(o)) {}

    // ── Type queries ────────────────────────────────────────────────
    bool isNull()   const { return std::holds_alternative<std::nullptr_t>(v_); }
    bool isBool()   const { return std::holds_alternative<bool>(v_); }
    bool isInt()    const { return std::holds_alternative<int64_t>(v_); }
    bool isDouble() const { return std::holds_alternative<double>(v_); }
    bool isString() const { return std::holds_alternative<std::string>(v_); }
    bool isArray()  const { return std::holds_alternative<JsonArray>(v_); }
    bool isObject() const { return std::holds_alternative<JsonObject>(v_); }
    bool isNumber() const { return isInt() || isDouble(); }

    // ── Value access ────────────────────────────────────────────────
    bool        asBool  (bool        def = false) const {
        return isBool() ? std::get<bool>(v_) : def;
    }
    int64_t     asInt   (int64_t     def = 0)     const {
        if (isInt())    return std::get<int64_t>(v_);
        if (isDouble()) return static_cast<int64_t>(std::get<double>(v_));
        return def;
    }
    double      asDouble(double      def = 0.0)   const {
        if (isDouble()) return std::get<double>(v_);
        if (isInt())    return static_cast<double>(std::get<int64_t>(v_));
        return def;
    }
    float       asFloat (float       def = 0.f)   const {
        return static_cast<float>(asDouble(static_cast<double>(def)));
    }
    const std::string& asString() const {
        static const std::string empty;
        return isString() ? std::get<std::string>(v_) : empty;
    }
    const JsonArray&  asArray()  const {
        static const JsonArray  empty;
        return isArray()  ? std::get<JsonArray>(v_)  : empty;
    }
    const JsonObject& asObject() const {
        static const JsonObject empty;
        return isObject() ? std::get<JsonObject>(v_) : empty;
    }

    // ── Object helpers ──────────────────────────────────────────────
    bool has(const std::string& k) const {
        return isObject() && std::get<JsonObject>(v_).count(k);
    }
    const JsonValue& operator[](const std::string& k) const {
        static const JsonValue null_;
        if (!isObject()) return null_;
        auto it = std::get<JsonObject>(v_).find(k);
        return it != std::get<JsonObject>(v_).end() ? it->second : null_;
    }
    JsonValue& operator[](const std::string& k) {
        return std::get<JsonObject>(v_)[k];
    }
    // Array access
    const JsonValue& operator[](int i) const { return (*this)[static_cast<size_t>(i)]; }
    JsonValue& operator[](int i) {
        if (i < 0) throw std::out_of_range("Negative JSON array index");
        return (*this)[static_cast<size_t>(i)];
    }
    JsonValue& operator[](size_t i) { return std::get<JsonArray>(v_).at(i); }
    const JsonValue& operator[](size_t i) const {
        static const JsonValue null_;
        if (!isArray()) return null_;
        auto& a = std::get<JsonArray>(v_);
        return i < a.size() ? a[i] : null_;
    }
    size_t size() const {
        if (isArray())  return std::get<JsonArray>(v_).size();
        if (isObject()) return std::get<JsonObject>(v_).size();
        return 0;
    }
    bool empty() const { return size() == 0; }

    const Var& var() const { return v_; }
    Var&       var()       { return v_; }

private:
    Var v_;
};

// ─── Convenience constructors ─────────────────────────────────────────
inline JsonObject obj() { return {}; }
inline JsonArray  arr() { return {}; }

// ═══════════════════════════════════════════════════════════════════════
// Writer
// ═══════════════════════════════════════════════════════════════════════

namespace detail {

inline std::string escStr(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b,8,"\\u%04x",c); o+=b; }
                else o += static_cast<char>(c);
        }
    }
    o += '"';
    return o;
}

inline void write(const JsonValue& v, std::string& out, int ind, int d) {
    const std::string nl  = ind > 0 ? "\n"  : "";
    const std::string sp  = ind > 0 ? std::string( d      * ind, ' ') : "";
    const std::string sp1 = ind > 0 ? std::string((d + 1) * ind, ' ') : "";

    switch (v.var().index()) {
        case 0: out += "null";  return;
        case 1: out += v.asBool() ? "true" : "false"; return;
        case 2: out += std::to_string(v.asInt()); return;
        case 3: {
            double x = v.asDouble();
            if (!std::isfinite(x)) {
                out += "null";
                return;
            }
            std::ostringstream number;
            number.imbue(std::locale::classic());
            number << std::setprecision(std::numeric_limits<double>::max_digits10) << x;
            auto text = number.str();
            if (text.find_first_of(".eE") == std::string::npos) text += ".0";
            out += text; return;
        }
        case 4: out += escStr(v.asString()); return;
        case 5: { // array
            auto& a = v.asArray();
            if (a.empty()) { out += "[]"; return; }
            out += "[" + nl;
            for (size_t i = 0; i < a.size(); ++i) {
                out += sp1; write(a[i], out, ind, d+1);
                if (i+1 < a.size()) out += ",";
                out += nl;
            }
            out += sp + "]"; return;
        }
        case 6: { // object
            auto& o = v.asObject();
            if (o.empty()) { out += "{}"; return; }
            out += "{" + nl;
            size_t i = 0;
            for (auto& [k, val] : o) {
                out += sp1 + escStr(k) + (ind > 0 ? ": " : ":");
                write(val, out, ind, d+1);
                if (i+1 < o.size()) out += ",";
                out += nl; ++i;
            }
            out += sp + "}"; return;
        }
    }
}

} // namespace detail

inline std::string toString(const JsonValue& v, bool pretty = true) {
    std::string s;
    detail::write(v, s, pretty ? 2 : 0, 0);
    return s;
}

// ═══════════════════════════════════════════════════════════════════════
// Parser
// ═══════════════════════════════════════════════════════════════════════

namespace detail {

struct Parser {
    std::string_view source;
    size_t pos = 0;
    static constexpr size_t MAX_DEPTH = 128;

    explicit Parser(std::string_view s) : source(s) {}
    [[noreturn]] static void fail() { throw std::runtime_error("JSON: invalid or incomplete input"); }
    static bool digit(char c) { return c >= '0' && c <= '9'; }
    void skip() {
        while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t' ||
               source[pos] == '\n' || source[pos] == '\r')) ++pos;
    }
    char peek() { skip(); return pos < source.size() ? source[pos] : '\0'; }
    char consume() { skip(); if (pos == source.size()) fail(); return source[pos++]; }

    unsigned hex4() {
        if (source.size() - pos < 4) fail();
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = source[pos++];
            unsigned value;
            if (c >= '0' && c <= '9') value = c - '0';
            else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
            else { fail(); }
            cp = (cp << 4) | value;
        }
        return cp;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else {
            if (cp < 0x800) out += static_cast<char>(0xC0 | (cp >> 6));
            else {
                if (cp < 0x10000) out += static_cast<char>(0xE0 | (cp >> 12));
                else {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                }
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            }
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    std::string parseString() {
        if (consume() != '"') fail();
        std::string out;
        while (pos < source.size()) {
            const auto c = static_cast<unsigned char>(source[pos++]);
            if (c == '"') return out;
            if (c < 0x20) fail();
            if (c == '\\') {
                if (pos == source.size()) fail();
                switch (source[pos++]) {
                    case '"': out += '"'; break; case '\\': out += '\\'; break;
                    case '/': out += '/'; break; case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break; case 't': out += '\t'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u': {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (source.substr(pos, 2) != "\\u") fail();
                            pos += 2;
                            const unsigned low = hex4();
                            if (low < 0xDC00 || low > 0xDFFF) fail();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) fail();
                        utf8(out, cp);
                        break;
                    }
                    default: fail();
                }
            } else if (c < 0x80) out += static_cast<char>(c);
            else {
                const unsigned count = c >= 0xC2 && c <= 0xDF ? 1 :
                    c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : 0;
                if (!count || source.size() - pos < count) fail();
                unsigned cp = c & (count == 1 ? 0x1F : count == 2 ? 0x0F : 0x07);
                for (unsigned i = 0; i < count; ++i) {
                    const auto next = static_cast<unsigned char>(source[pos++]);
                    if ((next & 0xC0) != 0x80) fail();
                    cp = (cp << 6) | (next & 0x3F);
                }
                if (cp < (count == 1 ? 0x80u : count == 2 ? 0x800u : 0x10000u) ||
                    cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) fail();
                utf8(out, cp);
            }
        }
        fail();
    }

    JsonValue parseNumber() {
        const size_t start = pos;
        bool flt = false;
        if (source[pos] == '-') ++pos;
        if (pos == source.size() || !digit(source[pos])) fail();
        if (source[pos] == '0') {
            ++pos;
            if (pos < source.size() && digit(source[pos])) fail();
        } else while (pos < source.size() && digit(source[pos])) ++pos;
        if (pos < source.size() && source[pos] == '.') {
            flt = true; ++pos;
            if (pos == source.size() || !digit(source[pos])) fail();
            while (pos < source.size() && digit(source[pos])) ++pos;
        }
        if (pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
            flt = true; ++pos;
            if (pos < source.size() && (source[pos] == '+' || source[pos] == '-')) ++pos;
            if (pos == source.size() || !digit(source[pos])) fail();
            while (pos < source.size() && digit(source[pos])) ++pos;
        }
        const auto num = source.substr(start, pos - start);
        if (flt) {
            std::istringstream input{std::string(num)};
            input.imbue(std::locale::classic());
            double value;
            if (!(input >> value) || !std::isfinite(value)) fail();
            return value;
        }
        int64_t value;
        const auto result = std::from_chars(num.data(), num.data() + num.size(), value);
        if (result.ec != std::errc{} || result.ptr != num.data() + num.size()) fail();
        return value;
    }

    JsonValue literal(std::string_view text, JsonValue value) {
        if (source.substr(pos, text.size()) != text) fail();
        pos += text.size();
        return value;
    }

    JsonValue parseValue(size_t depth = 0) {
        if (depth > MAX_DEPTH) fail();
        char c = peek();
        if ((c == '{' || c == '[') && depth >= MAX_DEPTH) fail();
        if (c=='"') return parseString();
        if (c=='{') return parseObject(depth);
        if (c=='[') return parseArray(depth);
        if (c=='t') return literal("true", true);
        if (c=='f') return literal("false", false);
        if (c=='n') return literal("null", nullptr);
        if (c=='-'||(c>='0'&&c<='9')) return parseNumber();
        fail();
    }

    JsonValue parseObject(size_t depth) {
        ++pos;
        JsonObject o;
        if (peek()=='}') { ++pos; return o; }
        while (true) {
            skip();
            if (peek()!='"') throw std::runtime_error("JSON: expected key string");
            std::string key = parseString();
            skip();
            if (consume()!=':') throw std::runtime_error("JSON: expected ':'");
            o[key] = parseValue(depth + 1);
            skip();
            char ch = peek();
            if (ch=='}') { ++pos; break; }
            if (ch==',') { ++pos; continue; }
            throw std::runtime_error("JSON: expected ',' or '}'");
        }
        return o;
    }

    JsonValue parseArray(size_t depth) {
        ++pos;
        JsonArray a;
        if (peek()==']') { ++pos; return a; }
        while (true) {
            a.push_back(parseValue(depth + 1));
            skip();
            char ch = peek();
            if (ch==']') { ++pos; break; }
            if (ch==',') { ++pos; continue; }
            throw std::runtime_error("JSON: expected ',' or ']'");
        }
        return a;
    }
};

} // namespace detail

inline JsonValue parse(std::string_view s) {
    detail::Parser p(s);
    auto value = p.parseValue();
    p.skip();
    if (p.pos != s.size()) detail::Parser::fail();
    return value;
}

// ─── File I/O ─────────────────────────────────────────────────────────

inline bool saveFile(const JsonValue& v, const std::string& path, bool pretty = true) {
    std::ofstream f(path);
    if (!f) return false;
    f << toString(v, pretty);
    return f.good();
}

inline JsonValue loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::string s((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
    return parse(s);
}

inline bool tryLoadFile(const std::string& path, JsonValue& out) {
    try { out = loadFile(path); return true; }
    catch (...) { return false; }
}

} // namespace npc::serial
