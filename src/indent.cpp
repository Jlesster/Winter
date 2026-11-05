#include "indent.hpp"
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include "columns.hpp"
#include "options.hpp"
#include "piece.hpp"
#include "syntax.hpp"

namespace {
constexpr std::string_view kCHeaders[] = {"if", "for", "while", "else", "do"};
constexpr std::string_view kProbe = "()[]{}\"'/";
constexpr size_t kMaxScan = 500;

struct Rules {
    std::span<const std::string_view> headers;
    bool preproc;
};

const Rules* rules_for(std::string_view lang) {
    static const Rules c{kCHeaders, true}, brace{kCHeaders, false},
        strict{{}, false};
    if(lang == "c" || lang == "cpp") return &c;
    for(const std::string_view l : {"java",
                                    "javascript",
                                    "typescript",
                                    "tsx",
                                    "c_sharp",
                                    "kotlin",
                                    "php",
                                    "dart",
                                    "scala"})
        if(lang == l) return &brace;
    if(lang == "go" || lang == "rust") return &strict;
    return nullptr;
}

bool model_lang(std::string_view l) { return l == "c" || l == "cpp"; }

bool is_closer(char c) { return c == ')' || c == ']' || c == '}'; }
bool is_opener(char c) { return c == '(' || c == '[' || c == '{'; }
bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string_view ltrim(std::string_view s) {
    return s.substr(std::min(s.find_first_not_of(" \t"), s.size()));
}

std::string_view rtrim(std::string_view s) {
    return s.substr(0, s.find_last_not_of(" \t") + 1);
}

std::string_view first_word(std::string_view h) {
    size_t i = 0;
    while(i < h.size() && ident_char(h[i])) ++i;
    return h.substr(0, i);
}

bool ends_with_word(std::string_view t, std::string_view w) {
    return t.ends_with(w) &&
           (t.size() == w.size() || !ident_char(t[t.size() - w.size() - 1]));
}

bool ends_cont(std::string_view t) {
    if(t.empty()) return false;
    const char c = t.back(), b = t.size() > 1 ? t[t.size() - 2] : 0;
    switch(c) {
        case '+':
        case '-': return b != c;
        case '=':
        case '*':
        case '/':
        case '%':
        case '?':
        case '.':
        case '|':
        case '^':
        case '&':
        case '\\': return true;
        case '<': return b == '<';
        case '>': return b == '-';
        default: return false;
    }
}

bool starts_cont(std::string_view h) {
    if(h.empty()) return false;
    if(h.starts_with("->") || h.starts_with("<<") || h.starts_with("&&") ||
       h.starts_with("||"))
        return true;
    if(h[0] == '?' || h[0] == ',') return true;
    if(h[0] == '.') return !h.starts_with("...");
    if(h[0] == ':') return !h.starts_with("::");
    return false;
}

bool starts_label(std::string_view h) {
    const std::string_view w = first_word(h);
    if(w == "case") return true;
    if(w != "default" && w != "public" && w != "private" && w != "protected")
        return false;
    const std::string_view rest = ltrim(h.substr(w.size()));
    return rest.starts_with(':') && !rest.starts_with("::");
}

struct Opener {
    size_t line, col;
    char ch;
};

class Scan {
public:
    Scan(Syntax& s, const PieceTable& pt, const Rules& r)
        : _syn(s),
          _pt(pt),
          _r(r) {}
    const std::string& text(size_t l) {
        auto [it, fresh] = _raw.try_emplace(l);
        if(fresh) it->second = _pt.line_text(l);
        return it->second;
    }

    const std::string& code(size_t l) {
        auto [it, fresh] = _code.try_emplace(l);
        std::string& s = it->second;
        if(!fresh) return s;
        s = text(l);
        if(_r.preproc && ltrim(s).starts_with('#') || pp_cont(l)) {
            s.assign(s.size(), ' ');
            return s;
        }
        const size_t base = _pt.line_start(l);
        bool seen = false;
        for(size_t i = 0; i < s.size();) {
            const char c = s[i];
            if(c == ' ' || c == '\t') {
                ++i;
                continue;
            }
            const bool probe = !seen || kProbe.contains(c);
            seen = true;
            const auto lit =
                probe ? _syn.literal_at(base + i) : Syntax::LitSpan{};
            if(lit.kind == Syntax::Lit::None) {
                ++i;
                continue;
            }
            const size_t to =
                std::min(s.size(), lit.end > base ? lit.end - base : i + 1);
            const char fill = lit.kind == Syntax::Lit::Comment ? ' ' : '_';
            for(size_t k = i; k < to; ++k) s[k] = fill;
            i = std::max(to, i + 1);
        }
        return s;
    }

    std::string_view head(size_t l) { return ltrim(code(l)); }
    std::string_view tail(size_t l) { return rtrim(code(l)); }
    bool blank(size_t l) { return head(l).empty(); }

    int ind(size_t l) {
        int c = 0;
        for(const char ch : text(l)) {
            if(ch == ' ') ++c;
            else if(ch == '\t') c += opts.tab_stop - c % opts.tab_stop;
            else break;
        }
        return c;
    }

    std::optional<size_t> prev_significant(size_t l) {
        while(l-- > 0)
            if(!blank(l)) return l;
        return std::nullopt;
    }

    std::optional<Opener> unclosed_before(size_t line, size_t col) {
        std::string seen;
        bool semi = false;
        size_t end = col;
        for(size_t l = line, n = 0;; --l, ++n) {
            const std::string& s = code(l);
            for(size_t i = std::min(end, s.size()); i-- > 0;) {
                const char c = s[i];
                if(c == ';' && seen.empty()) semi = true;
                else if(is_closer(c)) seen.push_back(c);
                else if(is_opener(c)) {
                    if(!seen.empty()) {
                        seen.pop_back();
                        continue;
                    }
                    if(c != '{' && semi &&
                       !ends_with_word(rtrim(std::string_view(s).substr(0, i)),
                                       "for"))
                        continue;
                    return Opener{l, i, c};
                }
            }
            if(l == 0 || n >= kMaxScan) break;
            end = std::string::npos;
        }
        return std::nullopt;
    }

    size_t anchor_line(size_t l, int depth = 0) {
        if(depth > 24) return l;
        const std::string& s = code(l);
        std::string open;
        std::optional<size_t> unmatched;
        for(size_t i = 0; i < s.size(); ++i) {
            if(is_opener(s[i])) open.push_back(s[i]);
            if(is_closer(s[i])) {
                if(open.empty()) unmatched = i;
                else open.pop_back();
            }
        }
        if(!unmatched) return l;
        const auto o = unclosed_before(l, *unmatched);
        return o ? anchor_line(o->line, depth + 1) : l;
    }

    size_t stmt_start(size_t p) {
        size_t l = anchor_line(p);
        const auto o = unclosed_before(l, 0);
        const size_t lo = o ? o->line + 1 : 0;
        while(l > lo) {
            const auto q = prev_significant(l);
            if(!q || *q < lo) break;
            if(!ends_cont(tail(*q)) && !starts_cont(head(l)) && !in_string(l) &&
               !(tail(*q).ends_with(',') && init_chain(*q)))
                break;
            l = anchor_line(*q);
        }
        return l;
    }

    size_t owner(const Opener& o) {
        const size_t l = anchor_line(o.line);
        const std::string_view h = head(l);
        return (h.starts_with(',') ||
                (h.starts_with(':') && !h.starts_with("::")))
                   ? stmt_start(l)
                   : l;
    }

    bool braceless_header(size_t p) {
        const std::string_view t = tail(p);
        if(t.empty()) return false;
        const std::string_view kw = first_word(head(anchor_line(p)));
        if(std::ranges::find(_r.headers, kw) == _r.headers.end()) return false;
        if(t.back() == ')') return true;
        return ends_with_word(t, "else") || ends_with_word(t, "do");
    }

    size_t pop_braceless(size_t p, size_t lo, bool once) {
        size_t base = anchor_line(p);
        while(base > lo) {
            const auto q = prev_significant(base);
            if(!q || *q < lo || !braceless_header(*q)) break;
            base = anchor_line(*q);
            if(once) break;
        }
        return base;
    }

    bool is_scope_block(size_t l) {  // namespace
        const std::string_view w = first_word(head(l));
        return w == "namespace" || (w == "extern" && text(l).contains('"'));
    }

    bool indent_ns(const Opener& o, size_t ow) {
        if(opts.indent_namespace != 2) return opts.indent_namespace == 1;
        for(size_t l = o.line + 1; l < _pt.line_count() && l < o.line + 50; ++l)
            if(!blank(l)) return ind(l) > ind(ow) && !is_closer(head(l)[0]);
        return false;
    }

    std::optional<int> pp_cont(size_t l) {  // line continue
        if(!_r.preproc || l == 0 || !rtrim(text(l - 1)).ends_with('\\'))
            return std::nullopt;
        size_t k = l - 1;
        while(k > 0 && rtrim(text(k - 1)).ends_with('\\')) --k;
        if(!ltrim(text(k)).starts_with('#')) return std::nullopt;
        return k == l - 1 ? opts.shift_width : ind(l - 1);
    }

    std::optional<size_t> init_chain(size_t q) {  // q ends in ','
        for(size_t n = 0; n < 64; ++n) {
            const std::string_view h = head(q);
            if(h.starts_with(':') && !h.starts_with("::")) return q;
            const auto p = prev_significant(q);
            if(!p || !tail(*p).ends_with(',')) return std::nullopt;
            q = *p;
        }
        return std::nullopt;
    }

    bool in_string(size_t l) {  // line starts inside a string
        const size_t ls = _pt.line_start(l);
        const Syntax::LitSpan lit = _syn.literal_at(ls);
        return lit.kind == Syntax::Lit::String && lit.start < ls;
    }

    // `case`/`default` labels of switch `o`: 0 flat, 1 indent, 2 follow
    // siblings
    bool indent_case(const Opener& o, size_t ow, size_t line) {
        if(opts.indent_case != 2) return opts.indent_case == 1;
        const auto sibling = [&](size_t l) -> std::optional<bool> {
            const std::string_view w = first_word(head(l));
            if(w != "case" && w != "default") return std::nullopt;
            const auto ob =
                unclosed_before(l, 0);  // must belong to THIS switch,
            if(!ob || ob->line != o.line ||
               ob->col != o.col)  // not a nested one
                return std::nullopt;
            return ind(l) > ind(ow);
        };
        for(size_t l = line; l-- > o.line + 1;)  // labels above first
            if(const auto r = sibling(l)) return *r;
        for(size_t l = line + 1; l < _pt.line_count() && l < line + 60; ++l)
            if(const auto r = sibling(l)) return *r;  // then below
        return true;                                  // none yet: indent
    }

    std::optional<int> align_col(const Opener& o) {
        const size_t k = code(o.line).find_first_not_of(" \t", o.col + 1);
        if(k == std::string::npos) return std::nullopt;
        return cx_to_rx(text(o.line), static_cast<int>(k));
    }

private:
    Syntax& _syn;
    const PieceTable& _pt;
    const Rules& _r;
    std::unordered_map<size_t, std::string> _raw, _code;
};

std::optional<int> structural(Syntax& syn, const PieceTable& pt, size_t line,
                              const Rules& r) {
    Scan sc(syn, pt, r);
    const int sw = opts.shift_width;
    const std::string_view cur = sc.head(line);
    if(r.preproc && ltrim(sc.text(line)).starts_with('#')) return 0;
    if(const auto pc = sc.pp_cont(line)) return *pc;
    const auto o = sc.unclosed_before(line, 0);
    const auto p = sc.prev_significant(line);
    const size_t lo = o ? o->line + 1 : 0;
    const auto inside = [&]() -> int {
        const auto ow = sc.owner(*o);
        if(o->ch == '{' && sc.is_scope_block(ow) && !sc.indent_ns(*o, ow))
            return sc.ind(ow);
        if(o->ch != '{')
            if(const auto a = sc.align_col(*o)) return *a;
        return sc.ind(ow) + sw;
    };
    const auto fix = [&](size_t base) {
        return (o && base < lo) ? inside() : sc.ind(base);
    };

    if(!cur.empty() && is_closer(cur[0])) {
        if(!o) return std::nullopt;
        return sc.ind(sc.owner(*o));
    }
    if(o && o->ch == '{' && starts_label(cur)) {
        const size_t ow = sc.owner(*o);
        const bool in =
            first_word(sc.head(ow)) == "switch" && sc.indent_case(*o, ow, line);
        return sc.ind(ow) + (in ? sw : 0);
    }
    if(o && p && *p == o->line) return inside();
    if(!p) return 0;
    if(starts_label(sc.head(*p)) && sc.tail(*p).ends_with(':') &&
       !starts_label(cur))
        return sc.ind(*p) + sw;
    const size_t s = sc.stmt_start(*p);
    if(cur.starts_with('{')) return fix(s);
    if(sc.tail(*p).ends_with(',') && (!o || o->ch == '{'))
        if(const auto k = sc.init_chain(*p))
            return sc.ind(*k) + (cur.starts_with(',') ? 0 : 2);
    if(ends_cont(sc.tail(*p)) || starts_cont(cur)) {
        const bool on_cont = *p != s && sc.anchor_line(*p) == *p;
        return on_cont ? sc.ind(*p) : fix(s) + sw;
    }
    if(first_word(cur) == "else") return fix(sc.pop_braceless(s, lo, true));
    if(sc.braceless_header(*p)) return fix(sc.anchor_line(*p)) + sw;
    return fix(sc.pop_braceless(s, lo, false));
}

bool is_label_prefix(std::string_view tok) {
    return starts_label(tok) && tok.ends_with(':');
}

}  // namespace

namespace indent {
std::string unit() {
    return opts.expand_tab
               ? std::string(static_cast<size_t>(opts.shift_width), ' ')
               : std::string("\t");
}

std::string_view leading_ws(std::string_view line) {
    size_t i = 0;
    while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.substr(0, i);
}

std::string ws_for_cols(int cols) {
    if(cols <= 0) return {};
    if(opts.expand_tab) return std::string(static_cast<size_t>(cols), ' ');
    return std::string(static_cast<size_t>(cols / opts.tab_stop), '\t') +
           std::string(static_cast<size_t>(cols % opts.tab_stop), ' ');
}

std::string for_newline(std::string_view before) {
    std::string out(leading_ws(before));
    const size_t last = before.find_last_not_of(" \t");
    if(last != std::string_view::npos && is_opener(before[last])) out += unit();
    return out;
}

std::string for_open_above(std::string_view line) {
    return std::string(leading_ws(line));
}

void reindent_lines(Model& m, Syntax& syn, PieceTable& pt, bool eol, size_t l0,
                    size_t l1) {
    PieceTable::Group g(pt);
    for(size_t l = l0; l <= l1; ++l) {
        const std::string line = pt.line_text(l);
        if(line.find_first_not_of(" \t") == std::string::npos) continue;
        const auto ws = from_tree(m, syn, pt, eol, l);
        if(!ws) continue;
        const std::string_view cur = leading_ws(line);
        if(cur == *ws) continue;
        const size_t at = pt.line_start(l);
        if(!cur.empty()) pt.remove(at, cur.size());
        if(!ws->empty()) pt.insert(at, *ws);
    }
}

std::optional<std::string> from_tree(Model& m, Syntax& syn, PieceTable& pt,
                                     bool eol, size_t line) {
    syn.update(pt, eol);
    const size_t ls = pt.line_start(line);
    const Syntax::LitSpan lit = syn.literal_at(ls);
    if(lit.kind != Syntax::Lit::None && lit.start < ls) {
        if(lit.kind == Syntax::Lit::String) return std::nullopt;
        const std::string t = pt.line_text(line);
        const size_t f = t.find_first_not_of(" \t");
        if(f == std::string::npos || t[f] != '*') return std::nullopt;
        const size_t ol = pt.line_of(lit.start);
        return ws_for_cols(
            cx_to_rx(pt.line_text(ol), int(lit.start - pt.line_start(ol))) + 1);
    }
    syn.update(pt, eol);
    const auto lang = syn.lang();
    const Rules* r = rules_for(lang);
    std::optional<int> st;
    if(model_lang(lang)) st = m.indent_for(syn, pt, line);
    else if(r) st = structural(syn, pt, line, *r);
    const std::optional<int> ts = syn.indent_for(line);
    const std::optional<int> cols = opts.indent_engine == 1   ? ts
                                    : opts.indent_engine == 2 ? st
                                                              : (st ? st : ts);
    if(std::getenv("WINTER_INDENT_LOG")) {
        const int legacy = (model_lang(lang) && r)
                               ? structural(syn, pt, line, *r).value_or(-1)
                               : st.value_or(-1);
        std::ofstream("/tmp/winter-indent.log", std::ios::app) << std::format(
            "line={} lang={} ie={} model={} legacy={} ts={} applied={} | {}\n",
            line + 1,
            lang,
            opts.indent_engine,
            st.value_or(-1),
            legacy,
            ts.value_or(-1),
            cols.value_or(-1),
            pt.line_text(line));
    }
    if(!cols) return std::nullopt;
    return ws_for_cols(*cols);
}

bool is_label(std::string_view tok) { return is_label_prefix(tok); }

}  // namespace indent
