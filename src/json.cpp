#include "json.hpp"
#include <charconv>
#include <cmath>
#include <format>

namespace {
const Json kNull;
const Json::Array kNoItems;
const Json::Object kNoMembers;
constexpr int kMaxDepth = 256;

void put_utf8(std::string& o, uint32_t c) {
    if(c < 0x80) {
        o += static_cast<char>(c);
    } else if(c < 0x800) {
        o += static_cast<char>(0xC0 | (c >> 6));
        o += static_cast<char>(0x80 | (c & 0x3F));
    } else if(c < 0x10000) {
        o += static_cast<char>(0xE0 | (c >> 12));
        o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (c >> 18));
        o += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Length of the valid UTF-8 sequence at s[i], or 0 if invalid.
size_t utf8_len(std::string_view s, size_t i) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const auto cont = [&](size_t k) {
        return k < s.size() && (b(k) & 0xC0) == 0x80;
    };
    const unsigned char c = b(i);
    if(c < 0x80) return 1;
    if(c >= 0xC2 && c <= 0xDF) return cont(i + 1) ? 2 : 0;
    if(c >= 0xE0 && c <= 0xEF) {
        if(!cont(i + 1) || !cont(i + 2)) return 0;
        if(c == 0xE0 && b(i + 1) < 0xA0) return 0;  // overlong
        if(c == 0xED && b(i + 1) > 0x9F) return 0;  // surrogate
        return 3;
    }
    if(c >= 0xF0 && c <= 0xF4) {
        if(!cont(i + 1) || !cont(i + 2) || !cont(i + 3)) return 0;
        if(c == 0xF0 && b(i + 1) < 0x90) return 0;  // overlong
        if(c == 0xF4 && b(i + 1) > 0x8F) return 0;  // > U+10FFFF
        return 4;
    }
    return 0;
}

// Invalid UTF-8 (a buffer can hold anything) becomes U+FFFD so one bad
// byte can't make a server reject the whole message.
void put_string(std::string& o, std::string_view s) {
    o += '"';
    for(size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if(c >= 0x80) {
            const size_t n = utf8_len(s, i);
            if(n == 0) {
                o += "\\ufffd";
                ++i;
            } else {
                o.append(s.data() + i, n);
                i += n;
            }
            continue;
        }
        ++i;
        switch(c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if(c < 0x20) o += std::format("\\u{:04x}", c);
                else o += static_cast<char>(c);
        }
    }
    o += '"';
}

struct Parser {
    std::string_view s{};
    size_t i = 0;
    int depth = 0;
    std::string err{};

    bool fail(const char* m) {
        if(err.empty()) err = std::format("{} at offset {}", m, i);
        return false;
    }
    void ws() {
        while(i < s.size() &&
              (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    bool lit(std::string_view w) {
        if(s.substr(i, w.size()) != w) return fail("bad literal");
        i += w.size();
        return true;
    }

    bool hex4(uint32_t& out) {
        if(i + 4 > s.size()) return fail("short \\u escape");
        out = 0;
        for(int k = 0; k < 4; ++k) {
            const char c = s[i + static_cast<size_t>(k)];
            out <<= 4;
            if(c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if(c >= 'a' && c <= 'f')
                out |= static_cast<uint32_t>(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F')
                out |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("bad hex digit");
        }
        i += 4;
        return true;
    }

    bool string(std::string& out) {
        ++i;  // opening quote
        for(;;) {
            if(i >= s.size()) return fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if(c == '"') {
                ++i;
                return true;
            }
            if(c < 0x20) return fail("control character in string");
            if(c != '\\') {
                out += static_cast<char>(c);
                ++i;
                continue;
            }
            if(++i >= s.size()) return fail("unterminated escape");
            const char e = s[i++];
            switch(e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if(!hex4(cp)) return false;
                    if(cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t lo = 0;
                        const size_t save = i;
                        if(s.substr(i, 2) == "\\u") {
                            i += 2;
                            if(!hex4(lo)) return false;
                        }
                        if(lo >= 0xDC00 && lo <= 0xDFFF)
                            cp =
                                0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else {
                            i = save;  // lone high surrogate
                            cp = 0xFFFD;
                        }
                    } else if(cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
    }

    bool number(Json& out) {
        const size_t start = i;
        bool is_int = true;
        if(s[i] == '-') ++i;
        if(i >= s.size()) return fail("bad number");
        if(s[i] == '0') ++i;
        else if(s[i] >= '1' && s[i] <= '9')
            while(i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        else return fail("bad number");
        if(i < s.size() && s[i] == '.') {
            is_int = false;
            ++i;
            if(i >= s.size() || s[i] < '0' || s[i] > '9')
                return fail("bad fraction");
            while(i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if(i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            is_int = false;
            ++i;
            if(i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if(i >= s.size() || s[i] < '0' || s[i] > '9')
                return fail("bad exponent");
            while(i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        const char* b = s.data() + start;
        const char* e = s.data() + i;
        if(is_int) {
            int64_t n = 0;
            const auto r = std::from_chars(b, e, n);
            if(r.ec == std::errc{}) {
                out = Json(n);
                return true;
            }
        }
        double d = 0;
        const auto r = std::from_chars(b, e, d);
        if(r.ec != std::errc{} && r.ec != std::errc::result_out_of_range)
            return fail("bad number");
        out = Json(std::isfinite(d) ? d : 0.0);
        return true;
    }

    bool value(Json& out) {
        ws();
        if(i >= s.size()) return fail("unexpected end");
        switch(s[i]) {
            case '{': return object(out);
            case '[': return array(out);
            case '"': {
                std::string str;
                if(!string(str)) return false;
                out = Json(std::move(str));
                return true;
            }
            case 't':
                if(!lit("true")) return false;
                out = Json(true);
                return true;
            case 'f':
                if(!lit("false")) return false;
                out = Json(false);
                return true;
            case 'n':
                if(!lit("null")) return false;
                out = Json();
                return true;
            default:
                if(s[i] == '-' || (s[i] >= '0' && s[i] <= '9'))
                    return number(out);
                return fail("unexpected character");
        }
    }

    bool array(Json& out) {
        if(++depth > kMaxDepth) return fail("nesting too deep");
        ++i;
        Json::Array a;
        ws();
        if(i < s.size() && s[i] == ']') {
            ++i;
        } else {
            for(;;) {
                Json v;
                if(!value(v)) return false;
                a.push_back(std::move(v));
                ws();
                if(i >= s.size()) return fail("unterminated array");
                if(s[i] == ',') {
                    ++i;
                    continue;
                }
                if(s[i] == ']') {
                    ++i;
                    break;
                }
                return fail("expected , or ]");
            }
        }
        --depth;
        out = Json::from_array(std::move(a));
        return true;
    }

    bool object(Json& out) {
        if(++depth > kMaxDepth) return fail("nesting too deep");
        ++i;
        Json::Object o;
        ws();
        if(i < s.size() && s[i] == '}') {
            ++i;
        } else {
            for(;;) {
                ws();
                if(i >= s.size() || s[i] != '"')
                    return fail("expected object key");
                std::string key;
                if(!string(key)) return false;
                ws();
                if(i >= s.size() || s[i] != ':') return fail("expected :");
                ++i;
                Json v;
                if(!value(v)) return false;
                o.emplace_back(std::move(key), std::move(v));
                ws();
                if(i >= s.size()) return fail("unterminated object");
                if(s[i] == ',') {
                    ++i;
                    continue;
                }
                if(s[i] == '}') {
                    ++i;
                    break;
                }
                return fail("expected , or }");
            }
        }
        --depth;
        out = Json::from_object(std::move(o));
        return true;
    }
};
}  // namespace

Json Json::arr(std::initializer_list<Json> items) {
    Json j;
    j._v = Array(items);
    return j;
}
Json Json::obj(std::initializer_list<Member> members) {
    Json j;
    j._v = Object{};
    for(const auto& m : members) j.set(m.first, m.second);
    return j;
}
Json Json::from_array(Array a) {
    Json j;
    j._v = std::move(a);
    return j;
}
Json Json::from_object(Object o) {
    Json j;
    j._v = std::move(o);
    return j;
}

bool Json::as_bool(bool def) const {
    const auto* p = std::get_if<bool>(&_v);
    return p ? *p : def;
}
int64_t Json::as_int(int64_t def) const {
    if(const auto* p = std::get_if<int64_t>(&_v)) return *p;
    if(const auto* d = std::get_if<double>(&_v))
        if(std::isfinite(*d) && *d >= -9.2e18 && *d <= 9.2e18 &&
           *d == std::floor(*d))
            return static_cast<int64_t>(*d);
    return def;
}
double Json::as_double(double def) const {
    if(const auto* p = std::get_if<double>(&_v)) return *p;
    if(const auto* n = std::get_if<int64_t>(&_v))
        return static_cast<double>(*n);
    return def;
}
std::string_view Json::as_str(std::string_view def) const {
    const auto* p = std::get_if<std::string>(&_v);
    return p ? std::string_view(*p) : def;
}

const Json::Array& Json::items() const {
    const auto* p = std::get_if<Array>(&_v);
    return p ? *p : kNoItems;
}
const Json::Object& Json::members() const {
    const auto* p = std::get_if<Object>(&_v);
    return p ? *p : kNoMembers;
}
size_t Json::size() const {
    if(const auto* a = std::get_if<Array>(&_v)) return a->size();
    if(const auto* o = std::get_if<Object>(&_v)) return o->size();
    return 0;
}
bool Json::has(std::string_view key) const {
    const auto& m = members();
    for(auto it = m.rbegin(); it != m.rend(); ++it)
        if(it->first == key) return true;
    return false;
}
const Json& Json::operator[](std::string_view key) const {
    const auto& m = members();
    for(auto it = m.rbegin(); it != m.rend(); ++it)
        if(it->first == key) return it->second;
    return kNull;
}
const Json& Json::operator[](size_t i) const {
    const auto& a = items();
    return i < a.size() ? a[i] : kNull;
}

Json& Json::set(std::string key, Json v) {
    if(!is_object()) _v = Object{};
    auto& o = std::get<Object>(_v);
    for(auto& m : o)
        if(m.first == key) {
            m.second = std::move(v);
            return *this;
        }
    o.emplace_back(std::move(key), std::move(v));
    return *this;
}
Json& Json::push(Json v) {
    if(!is_array()) _v = Array{};
    std::get<Array>(_v).push_back(std::move(v));
    return *this;
}

void Json::dump_to(std::string& o) const {
    switch(_v.index()) {
        case 0: o += "null"; break;
        case 1: o += std::get<bool>(_v) ? "true" : "false"; break;
        case 2: o += std::to_string(std::get<int64_t>(_v)); break;
        case 3: {
            const double d = std::get<double>(_v);
            if(!std::isfinite(d)) {
                o += "null";
                break;
            }
            char buf[32];
            const auto r = std::to_chars(buf, buf + sizeof buf, d);
            o.append(buf, r.ptr);
            break;
        }
        case 4: put_string(o, std::get<std::string>(_v)); break;
        case 5: {
            o += '[';
            bool first = true;
            for(const Json& v : std::get<Array>(_v)) {
                if(!first) o += ',';
                first = false;
                v.dump_to(o);
            }
            o += ']';
            break;
        }
        case 6: {
            o += '{';
            bool first = true;
            for(const auto& [k, v] : std::get<Object>(_v)) {
                if(!first) o += ',';
                first = false;
                put_string(o, k);
                o += ':';
                v.dump_to(o);
            }
            o += '}';
            break;
        }
    }
}
std::string Json::dump() const {
    std::string o;
    dump_to(o);
    return o;
}

std::optional<Json> Json::parse(std::string_view text, std::string* err) {
    Parser p;
    p.s = text;
    Json out;
    if(p.value(out)) {
        p.ws();
        if(p.i == text.size()) return out;
        p.fail("trailing characters");
    }
    if(err) *err = p.err;
    return std::nullopt;
}
