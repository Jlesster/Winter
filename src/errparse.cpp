#include "errparse.hpp"

#include <cstring>
#include <map>
#include <utility>

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string_view trim_cr(std::string_view s) {
    while(!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

std::string_view ltrim(std::string_view s) {
    const size_t i = s.find_first_not_of(" \t");
    return i == std::string_view::npos ? std::string_view{} : s.substr(i);
}

bool parse_int(std::string_view s, size_t& i, int& out) {
    const size_t b = i;
    long v = 0;
    while(i < s.size() && is_digit(s[i]) && i - b < 9) v = v * 10 + (s[i++] - '0');
    if(i == b) return false;
    out = static_cast<int>(v);
    return true;
}

struct Loc {
    std::string_view file;
    int line = 0, col = 0;
    size_t end = 0;  // just past the location and its trailing ':'
};

// "file:line", "file:line:col", optionally followed by ':' and then a space or
// the end of the string. `file` may not contain ": " (that rules out ld's
// "in function `f': a.cpp:3:" prefix being read as one long file name).
bool parse_loc(std::string_view s, Loc& out) {
    for(size_t p = s.find(':'); p != std::string_view::npos;
        p = s.find(':', p + 1)) {
        if(p == 0 || p + 1 >= s.size() || !is_digit(s[p + 1])) continue;
        const std::string_view file = s.substr(0, p);
        if(file.find(": ") != std::string_view::npos) return false;
        size_t i = p + 1;
        int line = 0, col = 0;
        if(!parse_int(s, i, line)) continue;
        if(i < s.size() && s[i] != ':') continue;
        if(i < s.size() && i + 1 < s.size() && is_digit(s[i + 1])) {
            size_t j = i + 1;
            int c = 0;
            if(parse_int(s, j, c) && (j == s.size() || s[j] == ':')) {
                col = c;
                i = j;
            }
        }
        if(i < s.size() && s[i] == ':') ++i;
        if(i < s.size() && s[i] != ' ') continue;
        out = {file, line, col, i};
        return true;
    }
    return false;
}

QfEntry make_entry(const Loc& l, char type, std::string_view text) {
    return QfEntry{std::string(l.file), l.line, l.col, type,
                   std::string(ltrim(text))};
}

// path:line[:col]: error|warning|note: text   (gcc, clang, and anything that
// copies their format: ninja, make, cmake, meson), plus ld's undefined and
// multiple definition lines.
class Gcc final : public ErrParser {
public:
    std::optional<QfEntry> feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.empty() || l[0] == ' ' || l[0] == '\t') return {};
        if(l.starts_with("In file included from")) return {};
        Loc loc;
        if(!parse_loc(l, loc)) return {};
        const std::string_view rest = ltrim(l.substr(loc.end));
        static constexpr std::pair<std::string_view, char> kSev[] = {
            {"fatal error:", 'e'}, {"error:", 'e'},  {"warning:", 'w'},
            {"note:", 'n'},        {"remark:", 'n'}, {"info:", 'n'}};
        for(const auto& [pre, type] : kSev)
            if(rest.starts_with(pre))
                return make_entry(loc, type, rest.substr(pre.size()));
        if(rest.starts_with("required from") ||
           rest.starts_with("in expansion of"))
            return make_entry(loc, 'n', rest);
        if(rest.starts_with("undefined reference") ||
           rest.starts_with("multiple definition"))
            return make_entry(loc, 'e', rest);
        return {};
    }
};

// error[E0308]: text   followed by   --> path:line:col
class Rustc final : public ErrParser {
public:
    std::optional<QfEntry> feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.empty()) {
            have_ = false;  // diagnostics are separated by a blank line
            return {};
        }
        if(l[0] != ' ' && l[0] != '\t') {
            static constexpr std::pair<std::string_view, char> kHead[] = {
                {"error", 'e'}, {"warning", 'w'}, {"note", 'n'}, {"help", 'n'}};
            for(const auto& [pre, type] : kHead) {
                if(!l.starts_with(pre)) continue;
                size_t i = pre.size();
                if(i < l.size() && l[i] == '[') {
                    const size_t j = l.find(']', i);
                    if(j == std::string_view::npos) continue;
                    i = j + 1;
                }
                if(l.substr(i, 2) != ": ") continue;
                type_ = type;
                text_ = std::string(l.substr(i + 2));
                have_ = true;
                break;
            }
            return {};
        }
        const std::string_view r = ltrim(l);
        if(!have_ || !r.starts_with("--> ")) return {};
        Loc loc;
        if(!parse_loc(r.substr(4), loc)) return {};
        have_ = false;
        return make_entry(loc, type_, text_);
    }

private:
    bool have_ = false;
    char type_ = 'e';
    std::string text_;
};

// ./main.go:12:5: undefined: x   (also `go test` lines, which are indented)
class Go final : public ErrParser {
public:
    std::optional<QfEntry> feed(std::string_view l) override {
        l = ltrim(trim_cr(l));
        Loc loc;
        if(!parse_loc(l, loc) || !loc.file.ends_with(".go")) return {};
        if(loc.file.starts_with("./")) loc.file.remove_prefix(2);
        return make_entry(loc, 'e', l.substr(loc.end));
    }
};

// Fallback: any "path:line[:col]: text" where path looks like a path.
class Generic final : public ErrParser {
public:
    std::optional<QfEntry> feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.empty() || l[0] == ' ' || l[0] == '\t') return {};
        if(l.starts_with("In file included from")) return {};
        Loc loc;
        if(!parse_loc(l, loc)) return {};
        const bool pathish = loc.file.find('.') != std::string_view::npos ||
                             loc.file.find('/') != std::string_view::npos ||
                             loc.file.ends_with("akefile");
        if(!pathish) return {};
        const std::string_view rest = ltrim(l.substr(loc.end));
        char type = 'e';
        if(rest.starts_with("warning")) type = 'w';
        else if(rest.starts_with("note")) type = 'n';
        return make_entry(loc, type, rest);
    }
};

std::map<std::string, ErrParserFactory, std::less<>>& table() {
    static std::map<std::string, ErrParserFactory, std::less<>> t;
    return t;
}

}  // namespace

void errparser_register(std::string name, ErrParserFactory factory) {
    table()[std::move(name)] = std::move(factory);
}

std::unique_ptr<ErrParser> errparser_make(std::string_view name) {
    const auto it = table().find(name);
    return it == table().end() ? nullptr : it->second();
}

std::vector<std::string> errparser_names() {
    std::vector<std::string> out;
    for(const auto& kv : table()) out.push_back(kv.first);
    return out;
}

void errparser_register_builtin() {
    errparser_register("gcc", [] { return std::make_unique<Gcc>(); });
    errparser_register("rustc", [] { return std::make_unique<Rustc>(); });
    errparser_register("go", [] { return std::make_unique<Go>(); });
    errparser_register("generic", [] { return std::make_unique<Generic>(); });
}

ErrChain::ErrChain(const std::vector<std::string>& names) {
    for(const std::string& n : names)
        if(auto p = errparser_make(n)) ps_.push_back(std::move(p));
    if(ps_.empty())
        if(auto p = errparser_make("generic")) ps_.push_back(std::move(p));
}

std::optional<QfEntry> ErrChain::feed(std::string_view line) {
    std::optional<QfEntry> hit;
    for(auto& p : ps_) {
        auto e = p->feed(line);
        if(!hit) hit = std::move(e);
    }
    return hit;
}
