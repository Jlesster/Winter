#include "comment.hpp"
#include <algorithm>
#include <cstddef>
#include <flat_map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "editor.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "piece.hpp"
#include "syntax.hpp"

namespace {
using Table = std::unordered_map<std::string, CommentStyle>;

Table& table() {
    static Table t = [] {
        Table m;
        const auto put = [&](std::initializer_list<const char*> langs,
                             CommentStyle s) {
            for(const char* l : langs) m[l] = s;
        };
        put({"c",      "cpp",        "rust",       "go",    "java",
             "d",      "javascript", "typescript", "tsx",   "swift",
             "kotlin", "scala",      "c_sharp",    "php",   "dart",
             "glsl",   "hlsl",       "cuda",       "jsonc", "proto"},
            {"//", "/*", "*/"});
        put({"zig"}, {"//", "", ""});
        put({"python",
             "bash",
             "fish",
             "nix",
             "toml",
             "yaml",
             "make",
             "cmake",
             "ruby",
             "perl",
             "dockerfile",
             "meson",
             "gitignore",
             "r"},
            {"#", "", ""});
        put({"lua", "sql", "haskell", "vhdl"}, {"--", "", ""});
        put({"vim"}, {"\"", "", ""});
        put({"ini", "scheme", "commonlisp", "clojure", "asm"}, {";", "", ""});
        put({"latex", "bibtex"}, {"%", "", ""});
        put({"html", "xml", "markdown", "svelte", "vue"}, {"", "<!--", "-->"});
        put({"css", "scss"}, {"", "/*", "*/"});
        return m;
    }();
    return t;
}

bool blank(const std::string& s) {
    return s.find_first_not_of(" \t") == std::string::npos;
}

size_t indent_of(const std::string& s) {
    const size_t i = s.find_first_not_of(" \t");
    return i == std::string::npos ? s.size() : i;
}

bool star_line(std::string_view r) {
    return r.starts_with('*') && (r.size() == 1 || r[1] == ' ' || r[1] == '\t');
}

bool in_star_block(PieceTable& pt, size_t line) {
    for(size_t l = line, n = 0; l-- > 0 && n < 512; ++n) {
        const std::string u = pt.line_text(l);
        const std::string_view r = std::string_view(u).substr(indent_of(u));
        if(r.starts_with("/*"))
            return r.find("*/", 2) == std::string_view::npos;
        if(!star_line(r)) return false;
    }
    return false;
}

}  // namespace

void comment_register(std::string lang, CommentStyle s) {
    table()[std::move(lang)] = std::move(s);
}

const CommentStyle* comment_style(std::string_view lang) {
    const auto it = table().find(std::string(lang));
    return it == table().end() ? nullptr : &it->second;
}

void comment_toggle(size_t l0, size_t l1) {
    Buffer& b = E.buf();
    PieceTable& pt = b.pt;
    const std::string_view lang = b.syntax.lang();
    const CommentStyle* st = comment_style(lang);
    if(!st) {
        editor_set_status_message("No comment strings for '{}'",
                                  lang.empty() ? "?" : std::string(lang));
        return;
    }
    l1 = std::min(l1, static_cast<size_t>(E.num_rows() - 1));
    const bool block = st->line.empty();
    const std::string& open = block ? st->open : st->line;
    const std::string& close = st->close;
    std::vector<std::string> txt;
    bool all_commented = true, any = false;
    size_t min_ind = std::string::npos;
    for(size_t l = l0; l <= l1; ++l) {
        txt.push_back(pt.line_text(l));
        const std::string& t = txt.back();
        if(blank(t)) continue;
        any = true;
        min_ind = std::min(min_ind, indent_of(t));
        const size_t i = indent_of(t);
        const bool starts = t.compare(i, open.size(), open) == 0;
        const bool ends =
            !block ||
            (t.size() >= close.size() &&
             t.compare(t.size() - close.size(), close.size(), close) == 0);
        if(!(starts && ends)) all_commented = false;
    }
    if(!any) return;
    PieceTable::Group g(pt);
    for(size_t l = l1 + 1; l-- > l0;) {
        const std::string& t = txt[l - l0];
        if(blank(t)) continue;
        const size_t ls = pt.line_start(l);
        if(!all_commented) {
            if(block) pt.insert(ls + t.size(), " " + close);
            pt.insert(ls + min_ind, open + " ");
        } else {
            const size_t i = indent_of(t);
            if(block) {
                size_t n = close.size();
                if(t.size() > i + open.size() + n && t[t.size() - n - 1] == ' ')
                    ++n;
                pt.remove(ls + t.size() - n, n);
            }
            size_t n = open.size();
            if(i + n < t.size() && t[i + n] == ' ') ++n;
            pt.remove(ls + i, n);
        }
    }
    apply_motion(motion::to_first_nonblank(pt, pt.line_start(l0), 0));
}

std::optional<CommentCont> comment_continue(size_t line, size_t cx,
                                            bool above) {
    Buffer& b = E.buf();
    const CommentStyle* st = comment_style(b.syntax.lang());
    if(!st) return std::nullopt;
    const std::string t = b.pt.line_text(line);
    const size_t ind = indent_of(t);
    const std::string_view rest = std::string_view(t).substr(ind);
    if(rest.empty()) return std::nullopt;
    const bool star = st->open == "/*";
    size_t lead = 0;
    bool opener = false;
    if(star && rest.starts_with("/*")) {
        if(above || rest.find("*/", 2) != std::string_view::npos)
            return std::nullopt;
        opener = true;
        lead = rest.size() > 2 && (rest[2] == '*' || rest[2] == '!') ? 3 : 2;
    } else if(star && star_line(rest)) {
        if(t.find("*/") != std::string::npos || !in_star_block(b.pt, line))
            return std::nullopt;
        lead = 1;
    } else if(!st->line.empty() && rest.starts_with(st->line)) {
        lead = st->line.size();
        if(lead < rest.size() && (rest[lead] == st->line.back() ||
                                  (st->line == "//" && rest[lead] == '!')))
            ++lead;
    } else {
        return std::nullopt;
    }
    if(cx < ind + lead) return std::nullopt;
    const size_t sp_end =
        std::min(t.find_first_not_of(" \t", ind + lead), t.size());
    const bool lone = sp_end == t.size();
    if(opener) return CommentCont{t.substr(0, ind) + " * ", lone};
    std::string spacing = t.substr(ind + lead, sp_end - (ind + lead));
    if(line && spacing.empty()) spacing = " ";
    return CommentCont{t.substr(0, ind + lead) + spacing, lone};
}
