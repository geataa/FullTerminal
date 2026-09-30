#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cfloat>
#include <charconv>
#include <locale.h>
#include <string_view>

namespace ft::json {

enum class Type {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class Value {
public:
    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    int64_t intVal = 0;
    bool isInteger = false;
    std::string strVal;
    std::vector<Value> arrVal;
    std::map<std::string, Value> objVal;

    Value() : type(Type::Null) {}
    Value(std::nullptr_t) : type(Type::Null) {}
    Value(bool b) : type(Type::Boolean), boolVal(b) {}
    Value(int v) : type(Type::Number), numVal(v), intVal(v), isInteger(true) {}
    Value(int64_t v) : type(Type::Number), numVal(static_cast<double>(v)), intVal(v), isInteger(true) {}
    Value(double v) : type(Type::Number), numVal(v), intVal(clamp_to_int64(v)), isInteger(false) {}
    Value(const char* s) : type(Type::String), strVal(s ? s : "") {}
    Value(const std::string& s) : type(Type::String), strVal(s) {}
    Value(std::string_view s) : type(Type::String), strVal(s) {}
    Value(Type t) : type(t) {}

    static Value Array() { return Value(Type::Array); }
    static Value Object() { return Value(Type::Object); }

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Boolean; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    bool as_bool(bool def = false) const {
        return (type == Type::Boolean) ? boolVal : def;
    }

    int64_t as_int(int64_t def = 0) const {
        if (type == Type::Number) return intVal;
        return def;
    }

    double as_double(double def = 0.0) const {
        if (type == Type::Number) return numVal;
        return def;
    }

    std::string as_string(const std::string& def = "") const {
        return (type == Type::String) ? strVal : def;
    }

    bool has(const std::string& key) const {
        if (type != Type::Object) return false;
        return objVal.find(key) != objVal.end();
    }

    const Value& operator[](const std::string& key) const {
        static Value nullVal;
        if (type != Type::Object) return nullVal;
        auto it = objVal.find(key);
        if (it != objVal.end()) return it->second;
        return nullVal;
    }

    Value& operator[](const std::string& key) {
        if (type != Type::Object) {
            type = Type::Object;
            objVal.clear();
        }
        return objVal[key];
    }

    const Value& operator[](size_t index) const {
        static Value nullVal;
        if (type != Type::Array || index >= arrVal.size()) return nullVal;
        return arrVal[index];
    }

    Value& operator[](size_t index) {
        if (type != Type::Array) {
            type = Type::Array;
            arrVal.clear();
        }
        if (index >= arrVal.size()) {
            arrVal.resize(index + 1);
        }
        return arrVal[index];
    }

    void push_back(const Value& val) {
        if (type != Type::Array) {
            type = Type::Array;
            arrVal.clear();
        }
        arrVal.push_back(val);
    }

    size_t size() const {
        if (type == Type::Array) return arrVal.size();
        if (type == Type::Object) return objVal.size();
        if (type == Type::String) return strVal.size();
        return 0;
    }

    std::string dump() const {
        std::string out;
        serialize(out);
        return out;
    }

    // Kati ayristirici: yarim kalmis/bozuk satir (orn. kesilmis bir ft_exec komutu) asla
    // kismen doldurulmus bir nesne olarak donmez. Hata durumunda *ok=false ve Null doner.
    static Value parse(std::string_view input, bool* ok = nullptr) {
        Ctx c{input};
        skip_ws(c);
        Value v;
        if (c.i >= input.size()) c.fail = true;
        else v = parse_value(c, 0);
        skip_ws(c);
        if (c.i != input.size()) c.fail = true; // sondaki cop
        if (ok) *ok = !c.fail;
        return c.fail ? Value() : v;
    }

private:
    // Yigin tasmasi /EHsc ile yakalanamaz; derinlik siniri tek gercek koruma.
    static constexpr int kMaxDepth = 256;

    struct Ctx {
        std::string_view s;
        size_t i = 0;
        bool fail = false;
    };

    static int64_t clamp_to_int64(double v) {
        if (!(v == v)) return 0; // NaN
        if (v >= 9.2233720368547758e18) return INT64_MAX;
        if (v <= -9.2233720368547758e18) return INT64_MIN;
        return static_cast<int64_t>(v);
    }

    static void skip_ws(Ctx& c) {
        while (c.i < c.s.size() && (c.s[c.i] == ' ' || c.s[c.i] == '\t' || c.s[c.i] == '\r' || c.s[c.i] == '\n')) {
            ++c.i;
        }
    }

    static Value parse_value(Ctx& c, int depth) {
        skip_ws(c);
        if (c.i >= c.s.size()) { c.fail = true; return Value(); }
        char ch = c.s[c.i];
        if (ch == 'n') return parse_literal(c, "null", Value());
        if (ch == 't') return parse_literal(c, "true", Value(true));
        if (ch == 'f') return parse_literal(c, "false", Value(false));
        if (ch == '"') return parse_string(c);
        if (ch == '[' || ch == '{') {
            if (depth >= kMaxDepth) { c.fail = true; return Value(); }
            return ch == '[' ? parse_array(c, depth + 1) : parse_object(c, depth + 1);
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) return parse_number(c);
        c.fail = true;
        return Value();
    }

    static Value parse_literal(Ctx& c, std::string_view word, Value v) {
        if (c.s.substr(c.i, word.size()) == word) {
            c.i += word.size();
            return v;
        }
        c.fail = true;
        return Value();
    }

    static int hex4(Ctx& c) {
        if (c.i + 4 > c.s.size()) return -1;
        int v = 0;
        for (int k = 0; k < 4; ++k) {
            char h = c.s[c.i + k];
            v <<= 4;
            if (h >= '0' && h <= '9') v += (h - '0');
            else if (h >= 'a' && h <= 'f') v += (h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v += (h - 'A' + 10);
            else return -1;
        }
        c.i += 4;
        return v;
    }

    static void append_utf8(std::string& out, uint32_t cp) {
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

    static Value parse_string(Ctx& c) {
        ++c.i; // skip '"'
        std::string res;
        while (c.i < c.s.size()) {
            char ch = c.s[c.i++];
            if (ch == '"') {
                return Value(res);
            }
            if (ch != '\\') {
                res += ch;
                continue;
            }
            if (c.i >= c.s.size()) break;
            char esc = c.s[c.i++];
            switch (esc) {
                case '"': res += '"'; break;
                case '\\': res += '\\'; break;
                case '/': res += '/'; break;
                case 'b': res += '\b'; break;
                case 'f': res += '\f'; break;
                case 'n': res += '\n'; break;
                case 'r': res += '\r'; break;
                case 't': res += '\t'; break;
                case 'u': {
                    int cp = hex4(c);
                    if (cp < 0) { c.fail = true; return Value(); }
                    // Python json.dumps gibi ASCII kacisli istemciler emojiyi vekil cift olarak yollar;
                    // tek tek kodlamak gecersiz CESU-8 uretir.
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        const size_t save = c.i;
                        int lo = -1;
                        if (c.i + 1 < c.s.size() && c.s[c.i] == '\\' && c.s[c.i + 1] == 'u') {
                            c.i += 2;
                            lo = hex4(c);
                        }
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            c.i = save; // sonraki kacis kendi basina islenir
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD; // yalniz dusuk vekil
                    }
                    append_utf8(res, static_cast<uint32_t>(cp));
                    break;
                }
                default:
                    c.fail = true;
                    return Value();
            }
        }
        c.fail = true; // kapanmamis dize
        return Value();
    }

    static Value parse_number(Ctx& c) {
        const std::string_view s = c.s;
        size_t& i = c.i;
        const size_t start = i;
        bool isFloat = false;
        auto digits = [&]() {
            const size_t d0 = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            return i > d0;
        };
        if (s[i] == '-') ++i;
        if (i < s.size() && s[i] == '0') {
            ++i;
        } else if (!digits()) {
            c.fail = true;
            return Value();
        }
        if (i < s.size() && s[i] == '.') {
            isFloat = true;
            ++i;
            if (!digits()) { c.fail = true; return Value(); }
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            isFloat = true;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (!digits()) { c.fail = true; return Value(); }
        }
        const std::string numStr(s.substr(start, i - start));
        if (!isFloat) {
            int64_t v = 0;
            auto r = std::from_chars(numStr.data(), numStr.data() + numStr.size(), v);
            if (r.ec == std::errc()) return Value(v);
            // int64 disi tamsayi: double olarak devam
        }
        // strtod asla istisna atmaz; tasmada HUGE_VAL, alt tasmada 0 verir. "C" yerel ayari ile
        // ondalik ayirici her zaman '.'.
        static _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
        double d = cLocale ? _strtod_l(numStr.c_str(), nullptr, cLocale) : strtod(numStr.c_str(), nullptr);
        if (d > DBL_MAX) d = DBL_MAX;
        else if (d < -DBL_MAX) d = -DBL_MAX;
        return Value(d);
    }

    static Value parse_array(Ctx& c, int depth) {
        ++c.i; // skip '['
        Value arr(Type::Array);
        skip_ws(c);
        if (c.i < c.s.size() && c.s[c.i] == ']') {
            ++c.i;
            return arr;
        }
        for (;;) {
            arr.arrVal.push_back(parse_value(c, depth));
            if (c.fail) return Value();
            skip_ws(c);
            if (c.i < c.s.size() && c.s[c.i] == ',') {
                ++c.i;
            } else if (c.i < c.s.size() && c.s[c.i] == ']') {
                ++c.i;
                return arr;
            } else {
                c.fail = true;
                return Value();
            }
        }
    }

    static Value parse_object(Ctx& c, int depth) {
        ++c.i; // skip '{'
        Value obj(Type::Object);
        skip_ws(c);
        if (c.i < c.s.size() && c.s[c.i] == '}') {
            ++c.i;
            return obj;
        }
        for (;;) {
            skip_ws(c);
            if (c.i >= c.s.size() || c.s[c.i] != '"') { c.fail = true; return Value(); }
            Value keyVal = parse_string(c);
            if (c.fail) return Value();
            skip_ws(c);
            if (c.i >= c.s.size() || c.s[c.i] != ':') { c.fail = true; return Value(); }
            ++c.i; // skip ':'
            Value val = parse_value(c, depth);
            if (c.fail) return Value();
            obj.objVal[keyVal.strVal] = std::move(val);
            skip_ws(c);
            if (c.i < c.s.size() && c.s[c.i] == ',') {
                ++c.i;
            } else if (c.i < c.s.size() && c.s[c.i] == '}') {
                ++c.i;
                return obj;
            } else {
                c.fail = true;
                return Value();
            }
        }
    }

    // Protokol satiri her zaman gecerli UTF-8 olmali: arac ciktisindaki OEM/ikili baytlar,
    // 5 MB sinirinda yarim kalan diziler vb. U+FFFD'ye cevrilir (Python istemcileri
    // gecersiz UTF-8'de baglantiyi koparir).
    static void escape_string(std::string& out, std::string_view s) {
        out += '"';
        const size_t n = s.size();
        size_t i = 0;
        while (i < n) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20 || c == 0x7F) {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                            out += buf;
                        } else {
                            out += static_cast<char>(c);
                        }
                        break;
                }
                ++i;
                continue;
            }
            // RFC 3629: gecerli dizinin uzunlugu ve ikinci baytin izinli araligi
            size_t len = 0;
            unsigned char lo = 0x80, hi = 0xBF;
            if (c >= 0xC2 && c <= 0xDF) len = 2;
            else if (c == 0xE0) { len = 3; lo = 0xA0; }
            else if (c >= 0xE1 && c <= 0xEC) len = 3;
            else if (c == 0xED) { len = 3; hi = 0x9F; }
            else if (c >= 0xEE && c <= 0xEF) len = 3;
            else if (c == 0xF0) { len = 4; lo = 0x90; }
            else if (c >= 0xF1 && c <= 0xF3) len = 4;
            else if (c == 0xF4) { len = 4; hi = 0x8F; }
            bool valid = len != 0 && i + len <= n;
            for (size_t k = 1; valid && k < len; ++k) {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if (k == 1 ? (cc < lo || cc > hi) : ((cc & 0xC0) != 0x80)) valid = false;
            }
            if (valid) {
                out.append(s.data() + i, len);
                i += len;
            } else {
                out += "\xEF\xBF\xBD"; // U+FFFD
                ++i;
            }
        }
        out += '"';
    }

    void serialize(std::string& out) const {
        switch (type) {
            case Type::Null: out += "null"; break;
            case Type::Boolean: out += (boolVal ? "true" : "false"); break;
            case Type::Number: {
                if (isInteger) {
                    out += std::to_string(intVal);
                } else if (!(numVal == numVal) || numVal > DBL_MAX || numVal < -DBL_MAX) {
                    out += "null"; // JSON'da NaN/Inf yok
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.17g", numVal);
                    out += buf;
                }
                break;
            }
            case Type::String: {
                escape_string(out, strVal);
                break;
            }
            case Type::Array: {
                out += '[';
                for (size_t i = 0; i < arrVal.size(); ++i) {
                    if (i > 0) out += ',';
                    arrVal[i].serialize(out);
                }
                out += ']';
                break;
            }
            case Type::Object: {
                out += '{';
                bool first = true;
                for (const auto& [k, v] : objVal) {
                    if (!first) out += ',';
                    first = false;
                    escape_string(out, k);
                    out += ':';
                    v.serialize(out);
                }
                out += '}';
                break;
            }
        }
    }
};

} // namespace ft::json
