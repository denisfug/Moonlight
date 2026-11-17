// Small JSON DOM: parser with size/depth limits and a compact serializer.
// No exceptions: parse() reports errors through JsonError.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ml {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json null() { return Json(); }
    static Json boolean(bool v);
    static Json number(double v);
    static Json string(std::string v);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Typed reads with defaults (never fail).
    bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0) const { return type_ == Type::Number ? num_ : def; }
    int64_t asInt(int64_t def = 0) const;
    const std::string &asString() const;   // "" when not a string
    std::string str(const std::string &def) const { return type_ == Type::String ? str_ : def; }

    // Arrays
    size_t size() const;   // elements (array) or members (object), else 0
    const Json &operator[](size_t i) const;   // null when out of range / not an array
    Json &push(Json v);    // converts null to array

    // Objects (insertion order is kept)
    bool has(const std::string &key) const;
    const Json &operator[](const std::string &key) const;   // null when missing
    const Json &get(const std::string &key) const { return (*this)[key]; }
    Json &set(const std::string &key, Json v);   // converts null to object; replaces existing key
    bool erase(const std::string &key);
    const std::vector<std::string> &keys() const { return keys_; }
    const std::vector<Json> &items() const { return items_; }   // array elements / object values

    std::string dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<std::string> keys_;   // object keys, parallel to items_
    std::vector<Json> items_;
    void dumpTo(std::string &out) const;
};

struct JsonError {
    std::string message;
    size_t offset = 0;
    bool ok() const { return message.empty(); }
};

struct JsonLimits {
    size_t maxBytes = 8 * 1024 * 1024;
    int maxDepth = 64;
};

// Returns a null Json and fills err when the input is invalid.
Json parseJson(const std::string &text, JsonError *err = nullptr, JsonLimits limits = {});

std::string jsonEscape(const std::string &s);

}  // namespace ml
