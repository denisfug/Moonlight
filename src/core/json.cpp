#include "core/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ml {

namespace {
// Lazily created (no static constructors: keeps the PS4 binary free of .init_array dependencies).
// First use always happens on the main thread, before any worker thread exists.
const Json &nullJson()
{
    static const Json *p = nullptr;
    if (!p) p = new Json();
    return *p;
}
const std::string &emptyString()
{
    static const std::string *p = nullptr;
    if (!p) p = new std::string();
    return *p;
}
}

Json Json::boolean(bool v) { Json j; j.type_ = Type::Bool; j.bool_ = v; return j; }
Json Json::number(double v) { Json j; j.type_ = Type::Number; j.num_ = v; return j; }
Json Json::string(std::string v) { Json j; j.type_ = Type::String; j.str_ = std::move(v); return j; }
Json Json::array() { Json j; j.type_ = Type::Array; return j; }
Json Json::object() { Json j; j.type_ = Type::Object; return j; }

int64_t Json::asInt(int64_t def) const
{
    if (type_ != Type::Number || !std::isfinite(num_)) return def;
    if (num_ > 9.2e18 || num_ < -9.2e18) return def;
    return static_cast<int64_t>(num_);
}

const std::string &Json::asString() const { return type_ == Type::String ? str_ : emptyString(); }

size_t Json::size() const { return (type_ == Type::Array || type_ == Type::Object) ? items_.size() : 0; }

const Json &Json::operator[](size_t i) const
{
    if (type_ != Type::Array || i >= items_.size()) return nullJson();
    return items_[i];
}

Json &Json::push(Json v)
{
    if (type_ == Type::Null) type_ = Type::Array;
    items_.push_back(std::move(v));
    return items_.back();
}

bool Json::has(const std::string &key) const
{
    if (type_ != Type::Object) return false;
    for (const auto &k : keys_) if (k == key) return true;
    return false;
}

const Json &Json::operator[](const std::string &key) const
{
    if (type_ != Type::Object) return nullJson();
    for (size_t i = 0; i < keys_.size(); i++) if (keys_[i] == key) return items_[i];
    return nullJson();
}

Json &Json::set(const std::string &key, Json v)
{
    if (type_ == Type::Null) type_ = Type::Object;
    for (size_t i = 0; i < keys_.size(); i++)
        if (keys_[i] == key) { items_[i] = std::move(v); return items_[i]; }
    keys_.push_back(key);
    items_.push_back(std::move(v));
    return items_.back();
}

bool Json::erase(const std::string &key)
{
    for (size_t i = 0; i < keys_.size(); i++)
        if (keys_[i] == key) {
            keys_.erase(keys_.begin() + static_cast<long>(i));
            items_.erase(items_.begin() + static_cast<long>(i));
            return true;
        }
    return false;
}

std::string jsonEscape(const std::string &s)
{
    std::string o;
    o.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o;
}

void Json::dumpTo(std::string &out) const
{
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
        if (!std::isfinite(num_)) { out += "null"; break; }
        char b[40];
        if (num_ == std::floor(num_) && std::fabs(num_) < 1e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(num_));
        else std::snprintf(b, sizeof b, "%.17g", num_);
        out += b;
        break;
    }
    case Type::String: out += '"'; out += jsonEscape(str_); out += '"'; break;
    case Type::Array:
        out += '[';
        for (size_t i = 0; i < items_.size(); i++) { if (i) out += ','; items_[i].dumpTo(out); }
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (size_t i = 0; i < items_.size(); i++) {
            if (i) out += ',';
            out += '"'; out += jsonEscape(keys_[i]); out += "\":";
            items_[i].dumpTo(out);
        }
        out += '}';
        break;
    }
}

std::string Json::dump() const { std::string s; dumpTo(s); return s; }

// ------------------------------------------------------------------ parser
namespace {

class Parser {
public:
    Parser(const std::string &t, JsonLimits l) : s_(t), lim_(l) {}
    JsonError err;

    Json run()
    {
        if (s_.size() > lim_.maxBytes) { fail("document too large"); return Json(); }
        ws();
        Json v = value(0);
        if (!err.ok()) return Json();
        ws();
        if (p_ != s_.size()) { fail("trailing characters"); return Json(); }
        return v;
    }

private:
    const std::string &s_;
    JsonLimits lim_;
    size_t p_ = 0;

    void fail(const char *m) { if (err.ok()) { err.message = m; err.offset = p_; } }
    void ws() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) p_++; }
    bool lit(const char *w)
    {
        size_t n = 0;
        while (w[n]) n++;
        if (s_.compare(p_, n, w) != 0) return false;
        p_ += n;
        return true;
    }

    Json value(int depth)
    {
        if (depth > lim_.maxDepth) { fail("nesting too deep"); return Json(); }
        if (p_ >= s_.size()) { fail("unexpected end"); return Json(); }
        char c = s_[p_];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') { std::string v; if (!string(v)) return Json(); return Json::string(std::move(v)); }
        if (c == 't') { if (lit("true")) return Json::boolean(true); }
        else if (c == 'f') { if (lit("false")) return Json::boolean(false); }
        else if (c == 'n') { if (lit("null")) return Json(); }
        else if (c == '-' || (c >= '0' && c <= '9')) return number();
        fail("unexpected character");
        return Json();
    }

    Json number()
    {
        size_t st = p_;
        if (s_[p_] == '-') p_++;
        if (p_ >= s_.size() || !(s_[p_] >= '0' && s_[p_] <= '9')) { fail("bad number"); return Json(); }
        if (s_[p_] == '0') p_++;
        else while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') p_++;
        if (p_ < s_.size() && s_[p_] == '.') {
            p_++;
            if (p_ >= s_.size() || !(s_[p_] >= '0' && s_[p_] <= '9')) { fail("bad number"); return Json(); }
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') p_++;
        }
        if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
            p_++;
            if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) p_++;
            if (p_ >= s_.size() || !(s_[p_] >= '0' && s_[p_] <= '9')) { fail("bad number"); return Json(); }
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') p_++;
        }
        std::string tok = s_.substr(st, p_ - st);
        return Json::number(std::strtod(tok.c_str(), nullptr));
    }

    static void utf8(std::string &o, uint32_t cp)
    {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(uint32_t &v)
    {
        if (p_ + 4 > s_.size()) { fail("bad \\u escape"); return false; }
        v = 0;
        for (int i = 0; i < 4; i++) {
            char c = s_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else { fail("bad \\u escape"); return false; }
        }
        return true;
    }

    bool string(std::string &o)
    {
        p_++;   // opening quote
        while (p_ < s_.size()) {
            unsigned char c = static_cast<unsigned char>(s_[p_]);
            if (c == '"') { p_++; return true; }
            if (c < 0x20) { fail("control character in string"); return false; }
            if (c != '\\') { o += static_cast<char>(c); p_++; continue; }
            p_++;
            if (p_ >= s_.size()) break;
            char e = s_[p_++];
            switch (e) {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t lo;
                    if (p_ + 1 < s_.size() && s_[p_] == '\\' && s_[p_ + 1] == 'u') {
                        p_ += 2;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else cp = 0xFFFD;
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                utf8(o, cp);
                break;
            }
            default: fail("bad escape"); return false;
            }
        }
        fail("unterminated string");
        return false;
    }

    Json array(int depth)
    {
        p_++;
        Json a = Json::array();
        ws();
        if (p_ < s_.size() && s_[p_] == ']') { p_++; return a; }
        for (;;) {
            ws();
            Json v = value(depth + 1);
            if (!err.ok()) return Json();
            a.push(std::move(v));
            ws();
            if (p_ < s_.size() && s_[p_] == ',') { p_++; continue; }
            if (p_ < s_.size() && s_[p_] == ']') { p_++; return a; }
            fail("expected , or ]");
            return Json();
        }
    }

    Json object(int depth)
    {
        p_++;
        Json o = Json::object();
        ws();
        if (p_ < s_.size() && s_[p_] == '}') { p_++; return o; }
        for (;;) {
            ws();
            if (p_ >= s_.size() || s_[p_] != '"') { fail("expected key"); return Json(); }
            std::string k;
            if (!string(k)) return Json();
            ws();
            if (p_ >= s_.size() || s_[p_] != ':') { fail("expected :"); return Json(); }
            p_++;
            ws();
            Json v = value(depth + 1);
            if (!err.ok()) return Json();
            o.set(k, std::move(v));
            ws();
            if (p_ < s_.size() && s_[p_] == ',') { p_++; continue; }
            if (p_ < s_.size() && s_[p_] == '}') { p_++; return o; }
            fail("expected , or }");
            return Json();
        }
    }
};

}  // namespace

Json parseJson(const std::string &text, JsonError *err, JsonLimits limits)
{
    Parser p(text, limits);
    Json v = p.run();
    if (err) *err = p.err;
    return v;
}

}  // namespace ml
