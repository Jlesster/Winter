#include "comment.hpp"
#include <algorithm>
#include <cstddef>
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
        const auto put = [&](std::initializer_list<const char*> langs, CommentStyle s) {
            for(const char* l : langs) m[l] = s;
        };
        put({"c", "cpp", "rust", "go", "java", "d", "javascript", "typescript",
            "tsx", "zig", "swift", "kotlin", "scala", "c_sharp", "php", "dart",
            "glsl", "hlsl", "cuda", "jsonc", "proto"},
            {"//", "", ""});
        put({"python", "bash", "fish", "nix", "toml", "yaml", "make", "cmake",
            "ruby", "perl", "dockerfile", "meson", "gitignore", "r"},
            {"#", "", ""});
        put({"lua", "sql", "haskell", "vhdl"}, {"--", "", ""});
        put({"vim"}, {"\"", "", ""});
        put({"ini", "scheme", "commonlisp", "clojure", "asm"}, {";", "", ""});
        put({"latex", "bibtex"}, {"%", "", ""});
        put({"html", "xml", "markdown", "svelte", "vue"},
            {"", "<!--", "-->"});
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

}

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
        const bool ends = !block || (t.size() >= close.size() &&
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
