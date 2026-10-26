#include "syntax.hpp"
#include <tree_sitter/api.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <regex>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "options.hpp"
#include "parser.hpp"
#include "piece.hpp"

#define X(n) extern "C" const TSLanguage* tree_sitter_##n();
WINTER_TS_LANGS(X)
#undef X

namespace {
struct LangEntry {
    std::string_view name;
    const TSLanguage* (*fn)();
};
const LangEntry kLangs[] = {
#define X(n) {#n, tree_sitter_##n},
    WINTER_TS_LANGS(X)
#undef X
};

struct ExtEntry {
    std::string_view ext, lang;
};
constexpr ExtEntry kExts[] = {
    {".lua", "lua"},
    {".c", "c"},
    {".h", "c"},
    {".cpp", "cpp"},
    {".cc", "cpp"},
    {".cxx", "cpp"},
    {".hpp", "cpp"},
    {".hh", "cpp"},
    {".java", "java"},
    {".rs", "rust"},
};

constexpr ExtEntry kNames[] = {
    {"Makefile", "make"},
    {"GNUmakefile", "make"},
    {"CMakeLists.txt", "cmake"},
    {"meson.build", "meson"},
    {"meson.options", "meson"},
    {"PKGBUILD", "bash"},
    {".bashrc", "bash"},
    {".bash_profile", "bash"},
    {".zshrc", "bash"},
};

struct ReadCtx {
    const PieceTable* pt;
    bool nl;
};

struct QueryEntry {
    std::string_view name, src;
};
const QueryEntry kQueries[] = {
#include "ts_queries.inc"
};
const QueryEntry kIndents[] = {
#include "ts_indents.inc"
};
constexpr std::pair<std::string_view, std::string_view> kInherits[] = {
    {"cpp", "c"}};

std::string_view query_src(std::string_view n) {
    for(const auto& q : kQueries)
        if(q.name == n) return q.src;
    return {};
}

std::string query_text(std::string_view n) {
    if(const auto s = query_src(n); !s.empty()) return std::string(s);
    return parser_query(n);
}

std::string indent_text(std::string_view n) {
    if(auto s = parser_query(n, "indents"); !s.empty()) return s;
    for(const auto& q : kIndents)
        if(q.name == n) return std::string(q.src);
    return {};
}

constexpr uint32_t MAUVE = 0xcba6f7, GREEN = 0xa6e3a1, PEACH = 0xfab387,
                   BLUE = 0x89b4fa, YELLOW = 0xf9e2af, RED = 0xf38ba8,
                   MAROON = 0xeba0ac, LAV = 0xb4befe, SKY = 0x89dceb,
                   SAPPH = 0x74c7ec, PINK = 0xf5c2e7, OV0 = 0x6c7086,
                   OV2 = 0x9399b2;

const std::unordered_map<std::string_view, Style> kTheme = {
    {"keyword", {.fg = MAUVE}},
    {"conditional", {.fg = MAUVE}},
    {"repeat", {.fg = MAUVE}},
    {"include", {.fg = MAUVE}},
    {"exception", {.fg = MAUVE}},
    {"storageclass", {.fg = MAUVE}},
    {"preproc", {.fg = PINK}},
    {"define", {.fg = PINK}},
    {"string", {.fg = GREEN}},
    {"character", {.fg = GREEN}},
    {"string.escape", {.fg = PINK}},
    {"escape", {.fg = PINK}},
    {"number", {.fg = PEACH}},
    {"float", {.fg = PEACH}},
    {"boolean", {.fg = PEACH}},
    {"constant", {.fg = PEACH}},
    {"function", {.fg = BLUE}},
    {"method", {.fg = BLUE}},
    {"function.builtin", {.fg = RED}},
    {"constructor", {.fg = SAPPH}},
    {"type", {.fg = YELLOW}},
    {"attribute", {.fg = YELLOW}},
    {"namespace", {.fg = LAV}},
    {"module", {.fg = LAV}},
    {"property", {.fg = LAV}},
    {"field", {.fg = LAV}},
    {"variable.parameter", {.fg = MAROON}},
    {"parameter", {.fg = MAROON}},
    {"variable.builtin", {.fg = RED}},
    {"label", {.fg = SAPPH}},
    {"operator", {.fg = SKY}},
    {"punctuation", {.fg = OV2}},
    {"comment", {.fg = OV0, .attrs = ATTR_ITALIC}},
};

std::optional<Style> theme_for(std::string_view cap) {
    while(!cap.empty()) {
        if(const auto it = kTheme.find(cap); it != kTheme.end())
            return it->second;
        const auto dot = cap.rfind('.');
        if(dot == std::string_view::npos) break;
        cap = cap.substr(0, dot);
    }
    return std::nullopt;
}

constexpr uint32_t kRainbow[] = {RED, PEACH, YELLOW, GREEN, SKY, BLUE, MAUVE};

bool is_generic(std::string_view t) {
    return t == "template_argument_list" || t == "template_parameter_list" ||
           t == "type_arguments" || t == "type_parameters";
}

void paint(std::vector<Span>& v, size_t x, uint32_t fg) {
    auto it = std::ranges::upper_bound(v, x, {}, &Span::start);
    if(it != v.begin() && std::prev(it)->end > x) {
        const Span p = *std::prev(it);
        it = v.erase(std::prev(it));
        Style st = p.st;
        st.fg = fg;
        if(p.start < x) it = std::next(v.insert(it, Span{p.start, x, p.st}));
        it = std::next(v.insert(it, Span{x, x + 1, st}));
        if(x + 1 < p.end) v.insert(it, Span{x + 1, p.end, p.st});
        return;
    }
    v.insert(it, Span{x, x + 1, Style{.fg = fg}});
}

std::string lua_to_ecma(std::string_view p) {
    std::string o;
    for(size_t i = 0; i < p.size(); ++i) {
        if(p[i] != '%' || i + 1 == p.size()) {
            o += p[i];
            continue;
        }
        switch(const char c = p[++i]) {
            case 'a': o += "[A-Za-z]"; break;
            case 'd': o += "\\d"; break;
            case 'l': o += "[a-z]"; break;
            case 'u': o += "[A-Z]"; break;
            case 'w': o += "[A-Za-z0-9]"; break;
            case 's': o += "\\s"; break;
            default: o += '\\'; o += c;
        }
    }
    return o;
}

const char* ts_read(void* payload, uint32_t byte, TSPoint, uint32_t* n) {
    auto* c = static_cast<ReadCtx*>(payload);
    const auto chunk = c->pt->chunk_at(byte);
    if(!chunk.empty()) {
        *n = static_cast<uint32_t>(chunk.size());
        return chunk.data();
    }
    static constexpr char nl = '\n';
    if(c->nl && byte == c->pt->size()) {
        *n = 1;
        return &nl;
    }
    *n = 0;
    return "";
}

}  // namespace

Syntax::Syntax()
    : _qc(ts_query_cursor_new()),
      _parser(ts_parser_new()) {}

Syntax::~Syntax() {
    if(_iquery) ts_query_delete(_iquery);
    if(_query) ts_query_delete(_query);
    ts_query_cursor_delete(_qc);
    if(_tree) ts_tree_delete(_tree);
    ts_parser_delete(_parser);
}

std::string_view Syntax::detect(std::string_view path) {
    const auto base = path.substr(path.rfind('/') + 1);
    for(const auto& n : kNames)
        if(n.ext == base) return n.lang;
    std::string ext;
    if(const auto dot = base.rfind('.');
       dot != std::string_view::npos && dot != 0) {
        ext = std::string(base.substr(dot));
        for(char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for(const auto& e : kExts)
            if(e.ext == ext) return e.lang;
    }
    if(const auto l = parser_lang_for_file(base); !l.empty()) return l;
    return ext.empty() ? std::string_view{} : parser_lang_for_ext(ext);
}

void Syntax::detach() {
    drop_tree();
    if(_iquery) ts_query_delete(_iquery);
    _iquery = nullptr;
    _ipreds.clear();
    _imeta.clear();
    _span_cache.clear();
    _name = {};
    _seen_rev = ~0ULL;
}

bool Syntax::attach(std::string_view filename) {
    detach();
    const std::string_view lang = detect(filename);
    if(lang.empty()) return false;
    const TSLanguage* tl = nullptr;
    for(const auto& l : kLangs)
        if(l.name == lang) {
            tl = l.fn();
            break;
        }
    if(!tl) tl = parser_load(lang);
    if(!tl) return false;
    if(!ts_parser_set_language(_parser, tl)) {
        _err = std::format("{}: grammar ABI not supported by this tree-sitter",
                           lang);
        return false;
    }
    drop_tree();
    _name = lang;
    load_query(tl, lang);
    load_indent_query(tl, lang);
    return true;
}

void Syntax::parse(const PieceTable& pt, bool trailing_nl) {
    _pt = &pt;
    if(_name.empty()) return;
    ReadCtx ctx{&pt, trailing_nl};
    const TSInput in{.payload = &ctx,
                     .read = ts_read,
                     .encoding = TSInputEncodingUTF8,
                     .decode = nullptr};
    TSTree* t = ts_parser_parse(_parser, _tree, in);
    if(_tree && t) {
        uint32_t n = 0;
        TSRange* r = ts_tree_get_changed_ranges(_tree, t, &n);
        for(uint32_t i = 0; i < n; ++i)
            cache_erase_range(r[i].start_byte, r[i].end_byte);
        std::free(r);
    } else {
        _span_cache.clear();
    }
    drop_tree();
    _tree = t;
    _nl = trailing_nl;
    _seen_rev = pt.revision();
}

bool Syntax::load_query(const TSLanguage* lang, std::string_view name) {
    if(_query) {
        ts_query_delete(_query);
        _query = nullptr;
    }
    auto build = [&](const std::string& src, uint32_t& off, TSQueryError& et) {
        return ts_query_new(
            lang, src.data(), static_cast<uint32_t>(src.size()), &off, &et);
    };
    std::string own = query_text(name), full;
    for(const auto& [from, base] : kInherits)
        if(from == name) { full = query_text(base) + "\n"; }
    full += own;
    uint32_t off = 0;
    TSQueryError et{};
    _query = build(full, off, et);
    if(!_query && full != own) _query = build(own, off, et);
    if(!_query) {
        _err =
            std::format("query error {} at byte {}", static_cast<int>(et), off);
        return false;
    }
    _err.clear();
    _cap_style.assign(ts_query_capture_count(_query), std::nullopt);
    for(uint32_t i = 0; i < _cap_style.size(); ++i) {
        uint32_t n;
        const char* c = ts_query_capture_name_for_id(_query, i, &n);
        _cap_style[i] = theme_for({c, n});
    }
    build_preds(_query, _preds, nullptr);
    return true;
}

bool Syntax::load_indent_query(const TSLanguage* lang, std::string_view name) {
    if(_iquery) ts_query_delete(_iquery);
    _iquery = nullptr;
    _ipreds.clear();
    _imeta.clear();
    std::string src;
    for(const auto& [from, base] : kInherits)
        if(from == name) src = indent_text(base) + "\n";
    src += indent_text(name);
    if(src.empty()) {
        _err = std::format("no indents query for {}, using heuristic", name);
        return false;
    }
    if(src.empty()) return false;
    uint32_t off = 0;
    TSQueryError et{};
    _iquery = ts_query_new(
        lang, src.data(), static_cast<uint32_t>(src.size()), &off, &et);
    if(!_iquery) {
        _err = std::format(
            "indents query error {} at byte {}", static_cast<int>(et), off);
        return false;
    }
    build_preds(_iquery, _ipreds, &_imeta);
    return true;
}

void Syntax::build_preds(TSQuery* q, std::vector<std::vector<Pred>>& out,
                         std::vector<Meta>* meta) {
    const uint32_t pc = ts_query_pattern_count(q);
    out.assign(pc, {});
    if(meta) meta->assign(pc, {});
    for(uint32_t p = 0; p < pc; ++p) {
        uint32_t n;
        const TSQueryPredicateStep* s =
            ts_query_predicates_for_pattern(q, p, &n);
        std::vector<const TSQueryPredicateStep*> cur;
        for(uint32_t i = 0; i < n; ++i) {
            if(s[i].type == TSQueryPredicateStepTypeDone) {
                add_pred(q, out, meta ? &(*meta)[p] : nullptr, p, cur);
                cur.clear();
            } else cur.push_back(&s[i]);
        }
    }
}

void Syntax::add_pred(TSQuery* q, std::vector<std::vector<Pred>>& out,
                      Meta* meta, uint32_t pattern,
                      const std::vector<const TSQueryPredicateStep*>& s) {
    auto str = [&](const TSQueryPredicateStep* st) {
        uint32_t n;
        const char* c = ts_query_string_value_for_id(q, st->value_id, &n);
        return std::string(c, n);
    };
    if(meta && s.size() >= 2 && s[0]->type == TSQueryPredicateStepTypeString &&
       str(s[0]) == "set!" && s[1]->type == TSQueryPredicateStepTypeString) {
        meta->emplace_back(
            str(s[1]),
            s.size() > 2 && s[2]->type == TSQueryPredicateStepTypeString
                ? str(s[2])
                : std::string("1"));
        return;
    }
    if(s.size() < 2 || s[0]->type != TSQueryPredicateStepTypeString ||
       s[1]->type != TSQueryPredicateStepTypeCapture)
        return;
    std::string name = str(s[0]);
    Pred p;
    p.cap = s[1]->value_id;
    if(name.starts_with("not-")) {
        p.negate = true;
        name.erase(0, 4);
    }
    if(name == "eq?" && s.size() == 3) {
        p.op = Pred::Eq;
        if(s[2]->type == TSQueryPredicateStepTypeCapture) {
            p.rhs_is_cap = true;
            p.rhs_cap = s[2]->value_id;
        } else p.rhs = str(s[2]);
    } else if((name == "match?" || name == "lua-match?" ||
               name == "vim-match?") &&
              s.size() == 3) {
        p.op = Pred::Match;
        std::string pat = str(s[2]);
        if(name == "lua-match?") pat = lua_to_ecma(pat);
        try {
            p.re =
                std::regex(pat, std::regex::ECMAScript | std::regex::optimize);
        } catch(...) { return; }
    } else if(name == "any-of?") {
        p.op = Pred::AnyOf;
        for(size_t i = 2; i < s.size(); ++i) p.any.push_back(str(s[i]));
    } else if(name == "kind-eq?" || name == "has-parent?" ||
              name == "has-ancestor?") {
        p.op = name == "kind-eq?"      ? Pred::KindEq
               : name == "has-parent?" ? Pred::HasParent
                                       : Pred::HasAncestor;
        for(size_t i = 2; i < s.size(); ++i) p.any.push_back(str(s[i]));
    } else {
        return;
    }
    out[pattern].push_back(std::move(p));
}

bool Syntax::eval(const std::vector<std::vector<Pred>>& preds,
                  const TSQueryMatch& m) const {
    auto node_of = [&](uint32_t cap) -> std::optional<TSNode> {
        for(uint16_t i = 0; i < m.capture_count; ++i)
            if(m.captures[i].index == cap) return m.captures[i].node;
        return std::nullopt;
    };
    auto text_of = [&](uint32_t cap) -> std::optional<std::string> {
        const auto n = node_of(cap);
        if(!n) return std::nullopt;
        const uint32_t a = ts_node_start_byte(*n);
        const uint32_t b = ts_node_end_byte(*n);
        return _pt->text(a, b - a);
    };
    auto kind_in = [](const Pred& p, const char* t) {
        return std::ranges::find(p.any, std::string_view(t)) != p.any.end();
    };
    for(const Pred& p : preds[m.pattern_index]) {
        bool ok = true;
        switch(p.op) {
            case Pred::KindEq:
            case Pred::HasParent:
            case Pred::HasAncestor: {
                const auto n = node_of(p.cap);
                if(!n) continue;
                if(p.op == Pred::KindEq) ok = kind_in(p, ts_node_type(*n));
                else if(p.op == Pred::HasParent) {
                    const TSNode par = ts_node_parent(*n);
                    ok = !ts_node_is_null(par) && kind_in(p, ts_node_type(par));
                } else {
                    ok = false;
                    for(TSNode a = ts_node_parent(*n);
                        !ok && !ts_node_is_null(a);
                        a = ts_node_parent(a))
                        ok = kind_in(p, ts_node_type(a));
                }
                break;
            }
            case Pred::Eq: {
                const auto lhs = text_of(p.cap);
                if(!lhs) continue;
                const auto rhs = p.rhs_is_cap
                                     ? text_of(p.rhs_cap)
                                     : std::optional<std::string>(p.rhs);
                ok = !rhs || *lhs == *rhs;
                break;
            }
            case Pred::Match: {
                const auto lhs = text_of(p.cap);
                if(!lhs) continue;
                ok = std::regex_search(*lhs, p.re);
                break;
            }
            case Pred::AnyOf: {
                const auto lhs = text_of(p.cap);
                if(!lhs) continue;
                ok = std::ranges::find(p.any, *lhs) != p.any.end();
                break;
            }
        }
        if(ok == p.negate) return false;
    }
    return true;
}

void Syntax::drop_tree() {
    if(_tree) ts_tree_delete(_tree);
    _tree = nullptr;
}

void Syntax::update(PieceTable& pt, bool trailing_nl) {
    _pt = &pt;
    const std::vector<TextEdit> edits = pt.drain_edits();
    if(_name.empty()) return;

    if(_tree && trailing_nl != _nl) drop_tree();
    if(pt.revision() != _seen_rev && edits.empty()) drop_tree();

    for(const TextEdit& e : edits) {
        if(!_tree) break;
        if(e.reset) {
            drop_tree();
            break;
        }
        const auto pt32 = [](const LineCol& p) {
            return TSPoint{static_cast<uint32_t>(p.line),
                           static_cast<uint32_t>(p.col)};
        };
        const TSInputEdit ie{static_cast<uint32_t>(e.start),
                             static_cast<uint32_t>(e.old_end),
                             static_cast<uint32_t>(e.new_end),
                             pt32(e.start_pt),
                             pt32(e.old_end_pt),
                             pt32(e.new_end_pt)};
        ts_tree_edit(_tree, &ie);
        cache_apply_edit(e);
    }
    if(pt.revision() == _seen_rev) return;
    parse(pt, trailing_nl);
}

void Syntax::cache_erase_range(size_t lo, size_t hi) {
    auto it = _span_cache.upper_bound(lo);
    if(it != _span_cache.begin()) {
        const auto prev = std::prev(it);
        if(prev->first + prev->second.len >= lo) it = prev;
    }
    while(it != _span_cache.end() && it->first <= hi)
        it = _span_cache.erase(it);
}

void Syntax::cache_apply_edit(const TextEdit& e) {
    cache_erase_range(e.start, e.old_end);
    if(e.new_end == e.old_end || _span_cache.empty()) return;
    auto it = _span_cache.upper_bound(e.old_end);
    std::vector<std::map<size_t, CacheLine>::node_type> moved;
    while(it != _span_cache.end()) moved.push_back(_span_cache.extract(it++));
    for(auto& n : moved) {
        n.key() = n.key() - e.old_end + e.new_end;
        _span_cache.insert(std::move(n));
    }
}

const std::vector<Span>& Syntax::base_spans(size_t ls, size_t len) {
    static const std::vector<Span> none;
    if(!_tree || !_query || len == 0) return none;
    if(const auto it = _span_cache.find(ls);
       it != _span_cache.end() && it->second.len == len)
        return it->second.spans;
    if(_span_cache.size() >= kSpanCacheCap) _span_cache.clear();
    const auto [it, inserted] = _span_cache.insert_or_assign(
        ls, CacheLine{len, compute_spans(ls, len)});
    (void)inserted;
    return it->second.spans;
}

void Syntax::rb_emit(size_t s, size_t e, size_t depth) {
    if(s < e && s >= _rb_lo && s < _rb_hi)
        _rb.push_back({s, static_cast<uint32_t>(depth)});
}

void Syntax::rb_walk(TSTreeCursor& c, size_t d, bool generic) {
    if(!ts_tree_cursor_goto_first_child(&c)) return;
    std::string st;
    do {
        const TSNode n = ts_tree_cursor_current_node(&c);
        const size_t s = ts_node_start_byte(n), e = ts_node_end_byte(n);
        if(s >= _rb_hi) break;
        if(ts_node_is_named(n)) {
            if(e > _rb_lo)
                rb_walk(c, d + st.size(), is_generic(ts_node_type(n)));
            continue;
        }
        const char* t = ts_node_type(n);
        if(!t[0] || t[1]) continue;
        switch(t[0]) {
            case '<':
                if(!generic) break;
                [[fallthrough]];
            case '(':
            case '[':
            case '{':
                rb_emit(s, e, d + st.size());
                st.push_back(t[0]);
                break;
            case ')':
            case ']':
            case '}':
            case '>': {
                const char o = t[0] == ')'   ? '('
                               : t[0] == ']' ? '['
                               : t[0] == '}' ? '{'
                                             : '<';
                if(st.empty() || st.back() != o) break;
                st.pop_back();
                rb_emit(s, e, d + st.size());
                break;
            }
            default: break;
        }
    } while(ts_tree_cursor_goto_next_sibling(&c));
    ts_tree_cursor_goto_parent(&c);
}

void Syntax::rainbow_prepare(size_t lo, size_t hi) {
    _rb.clear();
    if(!_tree || !opts.rainbow) return;
    _rb_lo = lo;
    _rb_hi = hi;
    TSTreeCursor c = ts_tree_cursor_new(ts_tree_root_node(_tree));
    rb_walk(c, 0, false);
    ts_tree_cursor_delete(&c);
}

const std::vector<Span>& Syntax::line_spans(size_t ls, size_t len) {
    const std::vector<Span>& base = base_spans(ls, len);
    if(_rb.empty() || len == 0) return base;
    auto it = std::ranges::lower_bound(_rb, ls, {}, &Delim::byte);
    if(it == _rb.end() || it->byte >= ls + len) return base;
    _merged = base;
    for(; it != _rb.end() && it->byte < ls + len; ++it)
        paint(
            _merged, it->byte - ls, kRainbow[it->depth % std::size(kRainbow)]);
    return _merged;
}

std::vector<Span> Syntax::compute_spans(size_t ls, size_t len) {
    std::vector<Span> out;
    if(!_tree || !_query || len == 0) return out;

    struct Cap {
        size_t s, e;
        uint32_t pat;
        Style st;
    };
    std::vector<Cap> caps;
    ts_query_cursor_set_byte_range(
        _qc, static_cast<uint32_t>(ls), static_cast<uint32_t>(ls + len));
    ts_query_cursor_exec(_qc, _query, ts_tree_root_node(_tree));

    TSQueryMatch m;
    uint32_t ci;
    while(ts_query_cursor_next_capture(_qc, &m, &ci)) {
        if(!eval(_preds, m)) {
            ts_query_cursor_remove_match(_qc, m.id);
            continue;
        }
        const TSQueryCapture& c = m.captures[ci];
        const auto& st = _cap_style[c.index];
        if(!st) continue;
        const size_t s = std::max<size_t>(ts_node_start_byte(c.node), ls);
        const size_t e = std::min<size_t>(ts_node_end_byte(c.node), ls + len);
        if(s < e) caps.push_back({s - ls, e - ls, m.pattern_index, *st});
    }
    std::ranges::stable_sort(caps, {}, &Cap::pat);

    std::vector<Style> bytes(len);
    std::vector<char> has(len, 0);
    for(const Cap& c : caps)
        for(size_t i = c.s; i < c.e; ++i) {
            bytes[i] = c.st;
            has[i] = 1;
        }

    for(size_t i = 0; i < len;) {
        if(!has[i]) {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while(j < len && has[j] && bytes[j] == bytes[i]) ++j;
        out.push_back({i, j, bytes[i]});
        i = j;
    }
    return out;
}

std::string Syntax::summary() const {
    if(!_err.empty()) return _err;
    if(!_tree) return {};
    const TSNode root = ts_tree_root_node(_tree);
    char* s = ts_node_string(root);
    std::string out = std::format("{} err={} {}",
                                  _name,
                                  ts_node_has_error(root),
                                  std::string_view(s).substr(0, 160));
    free(s);
    return out;
}
