/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "errparse.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

namespace {

using Kind = ErrEvent::Kind;
using TS = ErrTest::Status;

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

std::string_view rtrim(std::string_view s) {
    const size_t i = s.find_last_not_of(" \t");
    return i == std::string_view::npos ? std::string_view{} : s.substr(0, i + 1);
}

bool indented(std::string_view l) { return !l.empty() && (l[0] == ' ' || l[0] == '\t'); }

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

QfEntry make_entry(const Loc& l, char type, std::string_view text,
                   std::string_view source) {
    QfEntry e;
    e.file = std::string(l.file);
    e.line = l.line;
    e.col = l.col;
    e.type = type;
    e.text = std::string(ltrim(text));
    e.source = std::string(source);
    return e;
}

// "text [-Wunused-variable]" -> code "-Wunused-variable", text without it.
// javac: "[deprecation] text" -> code "deprecation".
void split_code(QfEntry& e) {
    std::string& t = e.text;
    if(t.size() > 3 && t.back() == ']') {
        const size_t b = t.rfind(" [");
        if(b != std::string::npos && b + 2 < t.size() - 1 && t[b + 2] == '-') {
            e.code = t.substr(b + 2, t.size() - b - 3);
            t.erase(b);
            return;
        }
    }
    if(t.size() > 2 && t[0] == '[') {
        const size_t j = t.find(']');
        if(j != std::string::npos && j > 1 && j < 40 &&
           t.find(' ') > j) {
            e.code = t.substr(1, j - 1);
            t.erase(0, j + 1);
            t.erase(0, t.find_first_not_of(' '));
        }
    }
}

// "12 |  code": a numbered snippet line, which rustc starts in column 0.
bool gutter_line(std::string_view l) {
    size_t i = 0;
    while(i < l.size() && is_digit(l[i])) ++i;
    if(i == 0) return false;
    while(i < l.size() && l[i] == ' ') ++i;
    return i < l.size() && l[i] == '|';
}

// A caret underline: "    ^~~~", "  5 |    ^~~~" or "  |    ^^^ label". col is
// 1-based in the source line printed above it.
bool caret_info(std::string_view l, int& col, int& end_col) {
    size_t i = 0;
    while(i < l.size() && l[i] == ' ') ++i;
    size_t j = i;
    while(j < l.size() && is_digit(l[j])) ++j;
    size_t k = j;
    while(k < l.size() && l[k] == ' ') ++k;
    const bool gutter = k < l.size() && l[k] == '|';
    const std::string_view rest = l.substr(gutter ? k + 1 : 0);
    // rustc puts secondary "---" labels left of the primary "^^^"
    const size_t c = rest.find_first_not_of(gutter ? " -" : " ");
    if(c == std::string_view::npos || rest[c] != '^') return false;
    size_t e = c;
    while(e < rest.size() && (rest[e] == '^' || rest[e] == '~')) ++e;
    col = static_cast<int>(c) + (gutter ? 0 : 1);
    end_col = col + static_cast<int>(e - c);
    return true;
}

ErrEvent context_event(std::string_view l) {
    ErrEvent v = ErrEvent::of_text(Kind::Context, std::string(l));
    caret_info(l, v.col, v.end_col);
    return v;
}

// path:line[:col]: error|warning|note: text   gcc, clang, javac, and whatever
// copies their format (ninja, make, cmake). Lines indented under a diagnostic
// are its snippet, caret and "symbol:" lines. Also ld's undefined and
// multiple definition lines.
class GccLike final : public ErrParser {
public:
    explicit GccLike(const char* source) : src_(source) {}
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.empty()) {
            ctx_ = false;
            return {};
        }
        if(indented(l) || (ctx_ && gutter_line(l)))
            return ctx_ ? context_event(l) : ErrEvent{};
        ctx_ = false;
        if(l.starts_with("In file included from")) return {};
        Loc loc;
        if(!parse_loc(l, loc)) return {};
        const std::string_view rest = ltrim(l.substr(loc.end));
        static constexpr std::pair<std::string_view, char> kSev[] = {
            {"fatal error:", 'e'}, {"error:", 'e'},  {"warning:", 'w'},
            {"note:", 'n'},        {"remark:", 'n'}, {"info:", 'n'}};
        for(const auto& [pre, type] : kSev)
            if(rest.starts_with(pre)) return emit(make_entry(loc, type, rest.substr(pre.size()), src_));
        if(rest.starts_with("required from") || rest.starts_with("in expansion of"))
            return emit(make_entry(loc, 'n', rest, src_));
        if(rest.starts_with("undefined reference") || rest.starts_with("multiple definition"))
            return emit(make_entry(loc, 'e', rest, src_));
        return {};
    }

private:
    ErrEvent emit(QfEntry e) {
        split_code(e);
        ctx_ = true;
        return ErrEvent::of_diag(std::move(e));
    }
    const char* src_;
    bool ctx_ = false;
};

// error[E0308]: text   followed by   --> path:line:col   and its frame, plus
// cargo's test lines and panic locations.
class Rustc final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.empty()) {
            have_ = ctx_ = panic_msg_ = false;  // diagnostics end at a blank line
            return {};
        }
        if(l.starts_with("test ")) {
            if(auto t = test_line(l)) return t;
        }
        if(l.starts_with("test result:"))
            return ErrEvent::of_text(Kind::Summary, std::string(l));
        if(l.starts_with("thread '")) {
            if(auto p = panic(l)) return p;
        }
        if(panic_msg_) {
            panic_msg_ = false;
            if(!indented(l)) return ErrEvent::of_text(Kind::Context, std::string(l));
        }
        if(ctx_ && gutter_line(l)) return context_event(l);
        if(!indented(l)) {
            ctx_ = false;
            static constexpr std::pair<std::string_view, char> kHead[] = {
                {"error", 'e'}, {"warning", 'w'}, {"note", 'n'}, {"help", 'n'}};
            for(const auto& [pre, type] : kHead) {
                if(!l.starts_with(pre)) continue;
                size_t i = pre.size();
                code_.clear();
                if(i < l.size() && l[i] == '[') {
                    const size_t j = l.find(']', i);
                    if(j == std::string_view::npos) continue;
                    code_ = std::string(l.substr(i + 1, j - i - 1));
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
        if(ctx_) return context_event(l);
        const std::string_view r = ltrim(l);
        if(!have_ || !r.starts_with("--> ")) return {};
        Loc loc;
        if(!parse_loc(r.substr(4), loc)) return {};
        have_ = false;
        ctx_ = true;
        QfEntry e = make_entry(loc, type_, text_, "rustc");
        e.code = code_;
        return ErrEvent::of_diag(std::move(e));
    }

private:
    static ErrEvent test_line(std::string_view l) {
        // "test mod::name ... ok|FAILED|ignored"
        const size_t d = l.rfind(" ... ");
        if(d == std::string_view::npos) return {};
        const std::string name(l.substr(5, d - 5));
        const std::string_view st = l.substr(d + 5);
        if(st.starts_with("ok")) return ErrEvent::of_test(TS::Pass, name);
        if(st.starts_with("FAILED")) return ErrEvent::of_test(TS::Fail, name);
        if(st.starts_with("ignored")) return ErrEvent::of_test(TS::Skip, name);
        return {};
    }

    ErrEvent panic(std::string_view l) {
        const size_t at = l.find(" panicked at ");
        if(at == std::string_view::npos) return {};
        std::string_view r = l.substr(at + 13);
        Loc loc;
        std::string msg = "panicked";
        if(!r.empty() && r[0] == '\'') {  // old: 'msg', file:line:col
            const size_t q = r.rfind("', ");
            if(q == std::string_view::npos) return {};
            msg = "panicked: " + std::string(r.substr(1, q - 1));
            r = r.substr(q + 3);
            if(!parse_loc(r, loc)) {  // file:line:col without a trailing ':'
                return {};
            }
        } else if(!parse_loc(r, loc)) {
            return {};
        } else {
            panic_msg_ = true;  // new style: the message is the next line
        }
        QfEntry e = make_entry(loc, 'e', msg, "rustc");
        return ErrEvent::of_diag(std::move(e));
    }

    bool have_ = false, ctx_ = false, panic_msg_ = false;
    char type_ = 'e';
    std::string text_, code_;
};

// ./main.go:12:5: undefined: x   (also `go test` lines, which are indented)
class Go final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.starts_with("--- FAIL: ")) return named(TS::Fail, l.substr(10));
        if(l.starts_with("--- PASS: ")) return named(TS::Pass, l.substr(10));
        if(l.starts_with("--- SKIP: ")) return named(TS::Skip, l.substr(10));
        if(l.starts_with("ok  \t") || l.starts_with("FAIL\t"))
            return ErrEvent::of_text(Kind::Summary, std::string(l));
        l = ltrim(l);
        Loc loc;
        if(!parse_loc(l, loc) || !loc.file.ends_with(".go")) return {};
        if(loc.file.starts_with("./")) loc.file.remove_prefix(2);
        return ErrEvent::of_diag(make_entry(loc, 'e', l.substr(loc.end), "go"));
    }

private:
    static ErrEvent named(TS s, std::string_view r) {
        return ErrEvent::of_test(s, std::string(r.substr(0, r.find(' '))));
    }
};

// e: file:///path/Foo.kt:12:5 message     (kotlinc through gradle)
class Kotlin final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        char type;
        if(l.starts_with("e: ")) type = 'e';
        else if(l.starts_with("w: ")) type = 'w';
        else return {};
        l.remove_prefix(3);
        if(l.starts_with("file://")) l.remove_prefix(7);
        for(size_t p = l.find(':'); p != std::string_view::npos; p = l.find(':', p + 1)) {
            size_t i = p + 1;
            int line = 0, col = 0;
            if(!parse_int(l, i, line) || i >= l.size() || l[i] != ':') continue;
            ++i;
            if(!parse_int(l, i, col)) continue;
            if(i < l.size() && l[i] == ':') ++i;
            QfEntry e;
            e.file = std::string(l.substr(0, p));
            e.line = line;
            e.col = col;
            e.type = type;
            e.text = std::string(ltrim(l.substr(i)));
            e.source = "kotlin";
            return ErrEvent::of_diag(std::move(e));
        }
        return {};
    }
};

// > Task :app:compileJava      BUILD FAILED in 3s
class Gradle final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.starts_with("> Task "))
            return ErrEvent::of_text(Kind::Progress, std::string(l.substr(2)));
        if(l.starts_with("BUILD FAILED") || l.starts_with("BUILD SUCCESSFUL"))
            return ErrEvent::of_text(Kind::Summary, std::string(l));
        return {};
    }
};

// CMake Error at CMakeLists.txt:12 (add_library):
//   message lines, indented
class CMake final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(pending_) {
            if(l.empty()) return {};  // the message may start after a blank line
            if(indented(l)) {
                e_.text = std::string(ltrim(l));
                pending_ = false;
                ctx_ = true;
                return ErrEvent::of_diag(std::move(e_));
            }
            ErrEvent out = flush();  // no message: the next line is not ours
            return out;
        }
        if(ctx_) {
            if(l.empty()) return {};
            if(indented(l)) return ErrEvent::of_text(Kind::Context, std::string(l));
            ctx_ = false;
        }
        char type;
        if(l.starts_with("CMake Error")) type = 'e';
        else if(l.starts_with("CMake Warning")) type = 'w';
        else return {};
        const size_t at = l.find(" at ");
        if(at == std::string_view::npos) return {};
        const std::string_view r = l.substr(at + 4);  // "file:line (func):"
        const size_t sp = r.find(' ');
        const std::string_view f = r.substr(0, sp);
        const size_t c = f.rfind(':');
        if(c == std::string_view::npos) return {};
        size_t i = c + 1;
        int ln = 0;
        if(!parse_int(f, i, ln)) return {};
        e_ = QfEntry{};
        e_.file = std::string(f.substr(0, c));
        e_.line = ln;
        e_.type = type;
        e_.source = "cmake";
        const size_t lp = r.find('(');
        const size_t rp = lp == std::string_view::npos ? lp : r.find(')', lp);
        if(rp != std::string_view::npos)
            e_.code = std::string(r.substr(lp + 1, rp - lp - 1));
        pending_ = true;
        return {};
    }
    ErrEvent finish() override { return pending_ ? flush() : ErrEvent{}; }

private:
    ErrEvent flush() {
        pending_ = false;
        ctx_ = false;
        e_.text = "in " + e_.code + "()";
        return ErrEvent::of_diag(std::move(e_));
    }
    bool pending_ = false, ctx_ = false;
    QfEntry e_;
};

// meson.build:12:0: ERROR: message
// Meson encountered an error in file meson.build, line 12, column 0:
class Meson final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(pending_) {
            pending_ = false;
            if(!l.empty()) {
                e_.text = std::string(l);
                return ErrEvent::of_diag(std::move(e_));
            }
        }
        Loc loc;
        if(parse_loc(l, loc)) {
            const std::string_view rest = ltrim(l.substr(loc.end));
            static constexpr std::pair<std::string_view, char> kSev[] = {
                {"ERROR:", 'e'}, {"WARNING:", 'w'}, {"NOTICE:", 'n'}, {"DEPRECATION:", 'w'}};
            for(const auto& [pre, type] : kSev)
                if(rest.starts_with(pre))
                    return ErrEvent::of_diag(
                        make_entry(loc, type, rest.substr(pre.size()), "meson"));
        }
        constexpr std::string_view kHead = "Meson encountered an error in file ";
        if(l.starts_with(kHead)) {
            const std::string_view r = l.substr(kHead.size());
            const size_t c = r.find(", line ");
            if(c == std::string_view::npos) return {};
            size_t i = c + 7;
            int ln = 0, col = 0;
            if(!parse_int(r, i, ln)) return {};
            if(r.substr(i, 9) == ", column ") {
                i += 9;
                parse_int(r, i, col);
            }
            e_ = QfEntry{};
            e_.file = std::string(r.substr(0, c));
            e_.line = ln;
            e_.col = col;
            e_.source = "meson";
            pending_ = true;
        }
        return {};
    }
    ErrEvent finish() override {
        if(!pending_) return {};
        pending_ = false;
        e_.text = "error";
        return ErrEvent::of_diag(std::move(e_));
    }

private:
    bool pending_ = false;
    QfEntry e_;
};

// [12/340] Building CXX object ...    [ 45%] Building C object ...
class Ninja final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.starts_with("ninja: build stopped") || l.starts_with("ninja: error"))
            return ErrEvent::of_text(Kind::Summary, std::string(l));
        if(l.size() < 5 || l[0] != '[') return {};
        size_t i = 1;
        while(i < l.size() && l[i] == ' ') ++i;
        const size_t d = i;
        while(i < l.size() && is_digit(l[i])) ++i;
        if(i == d || i >= l.size()) return {};
        if(l[i] == '/') {
            ++i;
            const size_t d2 = i;
            while(i < l.size() && is_digit(l[i])) ++i;
            if(i == d2 || i >= l.size() || l[i] != ']') return {};
        } else if(l[i] != '%' || i + 1 >= l.size() || l[i + 1] != ']') {
            return {};
        }
        return ErrEvent::of_text(Kind::Progress, std::string(l));
    }
};

//   1/10 Test  #1: name ..........   Passed    0.01 sec
class CTest final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = trim_cr(l);
        if(l.find("tests passed") != std::string_view::npos &&
           l.find("out of") != std::string_view::npos)
            return ErrEvent::of_text(Kind::Summary, std::string(ltrim(l)));
        const size_t t = l.find(" Test ");
        const size_t h = l.find('#');
        if(t == std::string_view::npos || h == std::string_view::npos || h < t) return {};
        const size_t c = l.find(": ", h);
        if(c == std::string_view::npos) return {};
        std::string_view r = l.substr(c + 2);
        const size_t dots = r.find(" ..");
        if(dots == std::string_view::npos) return {};
        const std::string name(r.substr(0, dots));
        r = r.substr(dots);
        if(r.find("Passed") != std::string_view::npos) return ErrEvent::of_test(TS::Pass, name);
        if(r.find("***Not Run") != std::string_view::npos || r.find("Skipped") != std::string_view::npos)
            return ErrEvent::of_test(TS::Skip, name);
        if(r.find("***") != std::string_view::npos) return ErrEvent::of_test(TS::Fail, name);
        return {};
    }
};

//  1/3 name                     OK       0.01s
class MesonTest final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
        l = rtrim(trim_cr(l));
        size_t i = 0;
        while(i < l.size() && l[i] == ' ') ++i;
        size_t d = i;
        while(i < l.size() && is_digit(l[i])) ++i;
        if(i == d || i >= l.size() || l[i] != '/') return {};
        ++i;
        d = i;
        while(i < l.size() && is_digit(l[i])) ++i;
        if(i == d || i >= l.size() || l[i] != ' ') return {};
        std::string_view r = ltrim(l.substr(i));
        // trailing "<status> <time>s"
        const size_t ts = r.rfind(' ');
        if(ts == std::string_view::npos || r.back() != 's') return {};
        const std::string_view body = rtrim(r.substr(0, ts));
        const size_t ss = body.rfind(' ');
        if(ss == std::string_view::npos) return {};
        const std::string_view status = body.substr(ss + 1);
        const std::string name(rtrim(body.substr(0, ss)));
        if(status == "OK" || status == "EXPECTEDFAIL") return ErrEvent::of_test(TS::Pass, name);
        if(status == "SKIP") return ErrEvent::of_test(TS::Skip, name);
        if(status == "FAIL" || status == "ERROR" || status == "TIMEOUT" ||
           status == "UNEXPECTEDPASS")
            return ErrEvent::of_test(TS::Fail, name);
        return {};
    }
};

// Fallback: any "path:line[:col]: text" where path looks like a path.
class Generic final : public ErrParser {
public:
    ErrEvent feed(std::string_view l) override {
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
        return ErrEvent::of_diag(make_entry(loc, type, rest, "generic"));
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

void errparser_remove(std::string_view name) {
    const auto it = table().find(name);
    if(it != table().end()) table().erase(it);
}

namespace {
std::vector<std::string>& default_chain() {
    static std::vector<std::string> c = {"gcc", "javac", "rustc", "go", "generic"};
    return c;
}
}  // namespace

const std::vector<std::string>& errparser_default_chain() { return default_chain(); }

void errparser_set_default_chain(std::vector<std::string> names) {
    default_chain() = std::move(names);
}

std::vector<std::string> errparser_names() {
    std::vector<std::string> out;
    for(const auto& kv : table()) out.push_back(kv.first);
    return out;
}

void errparser_register_builtin() {
    errparser_register("gcc", [] { return std::make_unique<GccLike>("gcc"); });
    errparser_register("javac", [] { return std::make_unique<GccLike>("javac"); });
    errparser_register("rustc", [] { return std::make_unique<Rustc>(); });
    errparser_register("go", [] { return std::make_unique<Go>(); });
    errparser_register("kotlin", [] { return std::make_unique<Kotlin>(); });
    errparser_register("gradle", [] { return std::make_unique<Gradle>(); });
    errparser_register("cmake", [] { return std::make_unique<CMake>(); });
    errparser_register("meson", [] { return std::make_unique<Meson>(); });
    errparser_register("ninja", [] { return std::make_unique<Ninja>(); });
    errparser_register("ctest", [] { return std::make_unique<CTest>(); });
    errparser_register("meson-test", [] { return std::make_unique<MesonTest>(); });
    errparser_register("generic", [] { return std::make_unique<Generic>(); });
}

ErrChain::ErrChain(const std::vector<std::string>& names) {
    for(const std::string& n : names)
        if(auto p = errparser_make(n)) ps_.push_back(std::move(p));
    if(ps_.empty())
        if(auto p = errparser_make("generic")) ps_.push_back(std::move(p));
}

ErrEvent ErrChain::finish() {
    for(auto& p : ps_)
        if(ErrEvent e = p->finish()) return e;
    return {};
}

ErrEvent ErrChain::feed(std::string_view line) {
    ErrEvent hit;
    for(auto& p : ps_) {
        ErrEvent e = p->feed(line);
        if(!hit) hit = std::move(e);
    }
    return hit;
}
