// DevDocs HTML -> markdown. See doc_core.hpp for where this sits.
//
// Two stages: a forgiving tag-soup parser that builds a small DOM, then a
// converter that walks it. DevDocs pages are machine-normalised fragments, so
// this does not try to be a browser; it handles what those pages contain and
// degrades to plain text for the rest.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include "doc_core.hpp"

namespace doc {
namespace {

// ---- small helpers --------------------------------------------------------

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool is_alnum(char c) { return is_alpha(c) || (c >= '0' && c <= '9'); }

std::string lower(std::string_view s) {
    std::string o(s);
    for(char& c : o)
        if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

std::string_view trim(std::string_view s) {
    while(!s.empty() && is_ws(s.front())) s.remove_prefix(1);
    while(!s.empty() && is_ws(s.back())) s.remove_suffix(1);
    return s;
}

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

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    size_t from = 0;
    while(true) {
        const size_t p = s.find(sep, from);
        if(p == std::string_view::npos) {
            out.push_back(s.substr(from));
            return out;
        }
        out.push_back(s.substr(from, p - from));
        from = p + 1;
    }
}

// ---- entities -------------------------------------------------------------

struct Ent {
    std::string_view name;
    uint32_t cp;
};
constexpr Ent kEnts[] = {
    {"amp", '&'},     {"lt", '<'},       {"gt", '>'},       {"quot", '"'},
    {"apos", '\''},   {"nbsp", 0xA0},    {"copy", 0xA9},    {"reg", 0xAE},
    {"trade", 0x2122}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026},
    {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
    {"laquo", 0xAB},  {"raquo", 0xBB},   {"bull", 0x2022},  {"middot", 0xB7},
    {"times", 0xD7},  {"divide", 0xF7},  {"plusmn", 0xB1},  {"deg", 0xB0},
    {"micro", 0xB5},  {"para", 0xB6},    {"sect", 0xA7},    {"euro", 0x20AC},
    {"pound", 0xA3},  {"yen", 0xA5},     {"cent", 0xA2},    {"larr", 0x2190},
    {"rarr", 0x2192}, {"uarr", 0x2191},  {"darr", 0x2193},  {"harr", 0x2194},
    {"lArr", 0x21D0}, {"rArr", 0x21D2},  {"hArr", 0x21D4},  {"le", 0x2264},
    {"ge", 0x2265},   {"ne", 0x2260},    {"infin", 0x221E}, {"minus", 0x2212},
    {"sum", 0x2211},  {"radic", 0x221A}, {"asymp", 0x2248}, {"equiv", 0x2261},
    {"alpha", 0x3B1}, {"beta", 0x3B2},   {"gamma", 0x3B3},  {"delta", 0x3B4},
    {"lambda", 0x3BB}, {"mu", 0x3BC},    {"pi", 0x3C0},     {"sigma", 0x3C3},
    {"omega", 0x3C9}, {"Omega", 0x3A9},  {"sup2", 0xB2},    {"sup3", 0xB3},
    {"frac12", 0xBD}, {"shy", 0},        {"zwnj", 0},       {"zwj", 0},
};

// nbsp reads as a plain space; zero-width characters vanish.
void put_text_cp(std::string& o, uint32_t cp) {
    if(cp == 0xA0) o += ' ';
    else if(cp == 0 || cp == 0xAD || cp == 0x200B || cp == 0x200C ||
            cp == 0x200D || cp == 0xFEFF)
        return;
    else if(cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) put_utf8(o, 0xFFFD);
    else put_utf8(o, cp);
}

std::string decode_entities(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for(size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if(c != '&') {
            o += c;
            continue;
        }
        const size_t semi = s.find(';', i + 1);
        if(semi == std::string_view::npos || semi - i > 12) {
            o += c;
            continue;
        }
        const std::string_view body = s.substr(i + 1, semi - i - 1);
        bool done = false;
        if(!body.empty() && body[0] == '#') {
            uint32_t cp = 0;
            bool ok = body.size() > 1;
            const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
            for(size_t k = hex ? 2 : 1; k < body.size() && ok; ++k) {
                const char d = body[k];
                int v;
                if(d >= '0' && d <= '9') v = d - '0';
                else if(hex && d >= 'a' && d <= 'f') v = d - 'a' + 10;
                else if(hex && d >= 'A' && d <= 'F') v = d - 'A' + 10;
                else {
                    ok = false;
                    break;
                }
                cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(v);
                if(cp > 0x10FFFF) ok = false;
            }
            if(ok && body.size() > (hex ? 2u : 1u)) {
                put_text_cp(o, cp);
                done = true;
            }
        } else {
            for(const Ent& e : kEnts)
                if(e.name == body) {
                    put_text_cp(o, e.cp);
                    done = true;
                    break;
                }
        }
        if(done) i = semi;
        else o += c;
    }
    return o;
}

// raw text out of the source: literal nbsp / zero-width bytes get the same
// treatment as their entities
std::string clean_text(std::string_view s) {
    std::string o = decode_entities(s);
    if(o.find('\xC2') == std::string::npos &&
       o.find('\xE2') == std::string::npos && o.find('\xEF') == std::string::npos)
        return o;
    std::string r;
    r.reserve(o.size());
    for(size_t i = 0; i < o.size(); ++i) {
        const auto u = static_cast<unsigned char>(o[i]);
        if(u == 0xC2 && i + 1 < o.size() &&
           static_cast<unsigned char>(o[i + 1]) == 0xA0) {
            r += ' ';
            ++i;
        } else if(u == 0xC2 && i + 1 < o.size() &&
                  static_cast<unsigned char>(o[i + 1]) == 0xAD) {
            ++i;
        } else if(u == 0xE2 && i + 2 < o.size() &&
                  static_cast<unsigned char>(o[i + 1]) == 0x80 &&
                  static_cast<unsigned char>(o[i + 2]) >= 0x8B &&
                  static_cast<unsigned char>(o[i + 2]) <= 0x8D) {
            i += 2;
        } else if(u == 0xEF && i + 2 < o.size() &&
                  static_cast<unsigned char>(o[i + 1]) == 0xBB &&
                  static_cast<unsigned char>(o[i + 2]) == 0xBF) {
            i += 2;
        } else {
            r += o[i];
        }
    }
    return r;
}

// ---- DOM ------------------------------------------------------------------

struct HNode {
    std::string tag, text;  // tag empty = text node
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<HNode>> kids;
    HNode* parent = nullptr;

    bool is_text() const { return tag.empty(); }
    const std::string* attr(std::string_view k) const {
        for(const auto& [n, v] : attrs)
            if(n == k) return &v;
        return nullptr;
    }
};

bool in(std::string_view t, std::initializer_list<std::string_view> l) {
    for(const std::string_view x : l)
        if(x == t) return true;
    return false;
}

bool is_void_tag(std::string_view t) {
    return in(t, {"area", "base", "br", "col", "embed", "hr", "img", "input",
                  "link", "meta", "param", "source", "track", "wbr"});
}

// Closes the nearest open element listed in `close` unless a `stop` element
// is in the way. st[0] is the root.
void close_nearest(std::vector<HNode*>& st,
                   std::initializer_list<std::string_view> close,
                   std::initializer_list<std::string_view> stop) {
    for(size_t i = st.size(); i-- > 1;) {
        const std::string& t = st[i]->tag;
        if(in(t, close)) {
            st.resize(i);
            return;
        }
        if(in(t, stop)) return;
    }
}

void implicit_close(std::vector<HNode*>& st, std::string_view tag) {
    if(tag == "li") close_nearest(st, {"li"}, {"ul", "ol", "menu"});
    else if(tag == "dt" || tag == "dd") close_nearest(st, {"dt", "dd"}, {"dl"});
    else if(tag == "tr") close_nearest(st, {"tr"}, {"table"});
    else if(tag == "td" || tag == "th") close_nearest(st, {"td", "th"}, {"tr", "table"});
    else if(tag == "thead" || tag == "tbody" || tag == "tfoot")
        close_nearest(st, {"thead", "tbody", "tfoot"}, {"table"});
    if(st.size() > 1 && st.back()->tag == "p" &&
       in(tag, {"address", "article", "aside", "blockquote", "div", "dl",
                "fieldset", "figure", "footer", "form", "h1", "h2", "h3", "h4",
                "h5", "h6", "header", "hr", "main", "nav", "ol", "p", "pre",
                "section", "table", "ul"}))
        st.pop_back();
}

constexpr size_t kMaxDepth = 400;  // open elements; deeper input is flattened

HNode* add_child(HNode* parent, std::unique_ptr<HNode> n) {
    n->parent = parent;
    parent->kids.push_back(std::move(n));
    return parent->kids.back().get();
}

void add_text(HNode* parent, std::string t) {
    if(t.empty()) return;
    if(!parent->kids.empty() && parent->kids.back()->is_text()) {
        parent->kids.back()->text += t;
        return;
    }
    auto n = std::make_unique<HNode>();
    n->text = std::move(t);
    add_child(parent, std::move(n));
}

std::unique_ptr<HNode> parse_html(std::string_view h) {
    auto root = std::make_unique<HNode>();
    root->tag = "#root";
    std::vector<HNode*> st{root.get()};
    const size_t n = h.size();
    size_t i = 0;
    while(i < n) {
        if(h[i] != '<') {
            size_t j = h.find('<', i);
            if(j == std::string_view::npos) j = n;
            add_text(st.back(), clean_text(h.substr(i, j - i)));
            i = j;
            continue;
        }
        if(h.compare(i, 4, "<!--") == 0) {
            const size_t e = h.find("-->", i + 4);
            i = e == std::string_view::npos ? n : e + 3;
            continue;
        }
        if(i + 1 < n && (h[i + 1] == '!' || h[i + 1] == '?')) {
            const size_t e = h.find('>', i);
            i = e == std::string_view::npos ? n : e + 1;
            continue;
        }
        if(i + 1 < n && h[i + 1] == '/') {
            const size_t e = h.find('>', i);
            if(e == std::string_view::npos) break;
            size_t a = i + 2, b = a;
            while(b < e && !is_ws(h[b])) ++b;
            const std::string name = lower(h.substr(a, b - a));
            for(size_t k = st.size(); k-- > 1;)
                if(st[k]->tag == name) {
                    st.resize(k);
                    break;
                }
            i = e + 1;
            continue;
        }
        if(i + 1 >= n || !is_alpha(h[i + 1])) {
            add_text(st.back(), "<");
            ++i;
            continue;
        }
        // start tag
        size_t j = i + 1;
        while(j < n && !is_ws(h[j]) && h[j] != '>' && h[j] != '/') ++j;
        auto node = std::make_unique<HNode>();
        node->tag = lower(h.substr(i + 1, j - i - 1));
        bool self_close = false;
        while(j < n) {
            while(j < n && is_ws(h[j])) ++j;
            if(j >= n) break;
            if(h[j] == '>') {
                ++j;
                break;
            }
            if(h[j] == '/') {
                self_close = true;
                ++j;
                continue;
            }
            const size_t ns = j;
            while(j < n && !is_ws(h[j]) && h[j] != '=' && h[j] != '>' &&
                  h[j] != '/')
                ++j;
            std::string an = lower(h.substr(ns, j - ns));
            std::string av;
            while(j < n && is_ws(h[j])) ++j;
            if(j < n && h[j] == '=') {
                ++j;
                while(j < n && is_ws(h[j])) ++j;
                if(j < n && (h[j] == '"' || h[j] == '\'')) {
                    const char q = h[j++];
                    const size_t e = h.find(q, j);
                    const size_t stop = e == std::string_view::npos ? n : e;
                    av = clean_text(h.substr(j, stop - j));
                    j = e == std::string_view::npos ? n : e + 1;
                } else {
                    const size_t vs = j;
                    while(j < n && !is_ws(h[j]) && h[j] != '>') ++j;
                    av = clean_text(h.substr(vs, j - vs));
                }
            }
            if(!an.empty()) node->attrs.emplace_back(std::move(an), std::move(av));
        }
        i = j;
        const std::string tag = node->tag;
        if(tag == "script" || tag == "style") {  // contents are not markup
            const std::string close = "</" + tag;
            size_t e = i;
            while(true) {
                e = h.find('<', e);
                if(e == std::string_view::npos) {
                    e = n;
                    break;
                }
                if(lower(h.substr(e, close.size())) == close) break;
                ++e;
            }
            const size_t gt = e < n ? h.find('>', e) : std::string_view::npos;
            i = gt == std::string_view::npos ? n : gt + 1;
            continue;
        }
        implicit_close(st, tag);
        HNode* p = add_child(st.back(), std::move(node));
        if(!is_void_tag(tag) && !self_close && st.size() < kMaxDepth)
            st.push_back(p);
    }
    return root;
}

// ---- classification -------------------------------------------------------

bool is_block(std::string_view t) {
    static const std::unordered_set<std::string_view> k = {
        "#root", "address", "article", "aside", "blockquote", "body", "caption",
        "center", "dd", "details", "dir", "div", "dl", "dt", "fieldset",
        "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5",
        "h6", "header", "hgroup", "hr", "html", "li", "main", "menu", "nav",
        "ol", "p", "pre", "section", "summary", "table", "tbody", "td", "tfoot",
        "th", "thead", "tr", "ul"};
    return k.contains(t);
}

bool is_skipped(std::string_view t) {
    static const std::unordered_set<std::string_view> k = {
        "script", "style", "head", "title", "noscript", "template", "svg",
        "button", "input", "select", "option", "textarea", "iframe", "object",
        "canvas", "audio", "video", "map", "form", "meta", "link", "base"};
    return k.contains(t);
}

bool has_block(const HNode& n) {
    for(const auto& k : n.kids) {
        if(k->is_text() || is_skipped(k->tag)) continue;
        if(is_block(k->tag) || has_block(*k)) return true;
    }
    return false;
}

bool has_rich_cell(const HNode& n) {  // content a table cell cannot hold
    for(const auto& k : n.kids) {
        if(k->is_text()) continue;
        if(in(k->tag, {"pre", "ul", "ol", "dl", "table", "blockquote"}) ||
           has_rich_cell(*k))
            return true;
    }
    return false;
}

// ---- paths and links ------------------------------------------------------

std::string slugify(std::string_view s) {
    std::string o;
    bool dash = false;
    for(const char c : s) {
        if(is_alnum(c)) {
            if(dash && !o.empty()) o += '-';
            dash = false;
            o += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        } else {
            dash = true;
        }
    }
    return o;
}

std::string rel_path(std::string_view from_stem, std::string_view to_stem) {
    auto fs = split(from_stem, '/');
    fs.pop_back();  // the file name
    const auto ts = split(to_stem, '/');
    size_t k = 0;
    while(k < fs.size() && k + 1 < ts.size() && fs[k] == ts[k]) ++k;
    std::string r;
    for(size_t i = k; i < fs.size(); ++i) r += "../";
    for(size_t i = k; i < ts.size(); ++i) {
        r += ts[i];
        if(i + 1 < ts.size()) r += '/';
    }
    return r;
}

struct Href {
    enum Kind { None, External, Frag, Page } kind = None;
    std::string url;   // External: as written
    std::string key;   // Page: db.json key
    std::string frag;  // Frag / Page: without the '#'
};

Href resolve_href(std::string_view href, const ConvertCtx& cx) {
    Href r;
    href = trim(href);
    if(href.empty()) return r;
    // scheme?
    size_t k = 0;
    while(k < href.size() && (is_alnum(href[k]) || href[k] == '+' ||
                              href[k] == '-' || href[k] == '.'))
        ++k;
    if(k > 0 && k < href.size() && href[k] == ':' && is_alpha(href[0])) {
        const std::string scheme = lower(href.substr(0, k));
        if(scheme == "http" || scheme == "https" || scheme == "ftp" ||
           scheme == "mailto") {
            r.kind = Href::External;
            r.url = std::string(href);
        }
        return r;
    }
    if(href.starts_with("//")) {
        r.kind = Href::External;
        r.url = "https:" + std::string(href);
        return r;
    }
    std::string_view path = href, frag;
    if(const size_t h = href.find('#'); h != std::string_view::npos) {
        path = href.substr(0, h);
        frag = href.substr(h + 1);
    }
    if(const size_t q = path.find('?'); q != std::string_view::npos)
        path = path.substr(0, q);
    if(path.empty()) {
        if(frag.empty()) return r;
        r.kind = Href::Frag;
        r.frag = pct_decode(frag);
        return r;
    }
    std::string base;
    if(path.front() == '/') {
        while(!path.empty() && path.front() == '/') path.remove_prefix(1);
    } else if(const size_t s = cx.page.rfind('/'); s != std::string::npos) {
        base = cx.page.substr(0, s);
    }
    std::vector<std::string> segs;
    for(const std::string_view p : split(base, '/'))
        if(!p.empty()) segs.emplace_back(p);
    const std::string decoded = pct_decode(path);
    for(const std::string_view p : split(decoded, '/')) {
        if(p.empty() || p == ".") continue;
        if(p == "..") {
            if(segs.empty()) return r;
            segs.pop_back();
        } else {
            segs.emplace_back(p);
        }
    }
    std::string norm;
    for(size_t i = 0; i < segs.size(); ++i) {
        if(i) norm += '/';
        norm += segs[i];
    }
    if(norm.empty()) norm = "index";
    std::string stem = norm;
    if(stem.ends_with(".html")) stem.resize(stem.size() - 5);
    const std::string cand[] = {norm, stem, norm + "/index", stem + "/index"};
    std::string found;
    for(const std::string& c : cand) {
        if(!cx.pages || cx.pages->contains(c)) {
            found = c;
            break;
        }
    }
    if(found.empty()) return r;
    if(found == cx.page) {
        if(frag.empty()) return r;
        r.kind = Href::Frag;
        r.frag = pct_decode(frag);
        return r;
    }
    r.kind = Href::Page;
    r.key = std::move(found);
    r.frag = pct_decode(frag);
    return r;
}

// ---- markdown text helpers ------------------------------------------------

// Plain text going into a paragraph: only what would be read as markup.
void esc_into(std::string& o, std::string_view s) {
    for(size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char prev = i ? s[i - 1] : ' ', next = i + 1 < s.size() ? s[i + 1] : ' ';
        switch(c) {
            case '\\':
            case '*':
            case '`':
            case '[':
            case ']': o += '\\'; break;
            case '<':
                if(is_alpha(next) || next == '/' || next == '!') o += '\\';
                break;
            case '_':
                if(!is_alnum(prev) || !is_alnum(next)) o += '\\';
                break;
            default: break;
        }
        o += c;
    }
}

// Appends text with whitespace collapsed to single spaces.
void append_text(std::string& out, std::string_view t) {
    std::string chunk;
    const auto flush = [&] {
        if(!chunk.empty()) esc_into(out, chunk);
        chunk.clear();
    };
    for(const char c : t) {
        if(is_ws(c)) {
            flush();
            if(!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
        } else {
            chunk += c;
        }
    }
    flush();
}

void trim_trailing_space(std::string& s) {
    while(!s.empty() && s.back() == ' ') s.pop_back();
}

std::string trim_copy(std::string_view s) { return std::string(trim(s)); }

std::string code_span(std::string_view text) {
    size_t run = 0, best = 0;
    for(const char c : text) {
        run = c == '`' ? run + 1 : 0;
        best = std::max(best, run);
    }
    const std::string d(best + 1, '`');
    const bool pad = !text.empty() && (text.front() == '`' || text.back() == '`');
    return d + (pad ? " " : "") + std::string(text) + (pad ? " " : "") + d;
}

std::string indent(std::string_view s, size_t n) {
    const std::string pad(n, ' ');
    std::string o;
    size_t from = 0;
    while(from <= s.size()) {
        size_t nl = s.find('\n', from);
        if(nl == std::string_view::npos) nl = s.size();
        const std::string_view line = s.substr(from, nl - from);
        if(!line.empty()) o += pad;
        o += line;
        if(nl < s.size()) o += '\n';
        from = nl + 1;
    }
    return o;
}

std::string fence_lang_clean(std::string_view l) {
    std::string o = lower(trim(l));
    if(o == "c++") o = "cpp";
    if(o == "js") o = "javascript";
    if(o == "ts") o = "typescript";
    if(o == "kt") o = "kotlin";
    std::string r;
    for(const char c : o)
        if(is_alnum(c) || c == '+' || c == '#' || c == '_' || c == '-') r += c;
    return r;
}

// ---- the converter --------------------------------------------------------

struct IState {
    bool flat = false;  // headings and cells: no line breaks
    bool strong = false, em = false, link = false;
    int depth = 0;
};

struct Blk {
    enum Kind { Para, List, Other } kind;
    std::string s;
};

class Conv {
public:
    explicit Conv(const ConvertCtx& c) : cx(c) {}

    std::vector<Blk> flow(const HNode& n, int depth) {
        std::vector<Blk> out;
        if(depth > 120) {  // absurd nesting: keep the words, drop the structure
            std::string t;
            append_text(t, raw_text(n, false));
            if(!trim(t).empty()) out.push_back({Blk::Para, trim_copy(t)});
            return out;
        }
        std::string inl;
        const auto flush = [&] {
            std::string p = tidy_para(inl);
            inl.clear();
            if(!p.empty()) out.push_back({Blk::Para, std::move(p)});
        };
        for(const auto& kp : n.kids) {
            const HNode& k = *kp;
            if(k.is_text()) {
                append_text(inl, k.text);
                continue;
            }
            if(is_skipped(k.tag)) continue;
            if(k.tag == "br") {
                trim_trailing_space(inl);
                inl += '\n';
                continue;
            }
            if(is_block(k.tag)) {
                flush();
                block_elem(k, out, depth);
            } else if(has_block(k)) {
                flush();
                for(Blk& b : flow(k, depth + 1)) out.push_back(std::move(b));
            } else {
                inline_elem(k, inl, IState{.depth = depth});
            }
        }
        flush();
        return out;
    }

    static std::string join(const std::vector<Blk>& bs, bool tight) {
        std::string o;
        for(size_t i = 0; i < bs.size(); ++i) {
            if(i) {
                const bool close = tight && bs[i - 1].kind == Blk::Para &&
                                   bs[i].kind == Blk::List;
                o += close ? "\n" : "\n\n";
            }
            o += bs[i].s;
        }
        return o;
    }

private:
    const ConvertCtx& cx;

    // ---- paragraphs ----

    static std::string tidy_para(const std::string& raw) {
        std::string o;
        size_t from = 0;
        bool first = true;
        while(from <= raw.size()) {
            size_t nl = raw.find('\n', from);
            if(nl == std::string::npos) nl = raw.size();
            std::string_view line = trim(std::string_view(raw).substr(from, nl - from));
            from = nl + 1;
            if(line.empty()) continue;
            if(!first) o += '\n';
            first = false;
            o += escape_line_start(line);
        }
        return o;
    }

    static std::string escape_line_start(std::string_view l) {
        if(l.front() == '#' || l.front() == '>')
            return "\\" + std::string(l);
        if((l.front() == '-' || l.front() == '+') && l.size() > 1 && l[1] == ' ')
            return "\\" + std::string(l);
        size_t d = 0;
        while(d < l.size() && l[d] >= '0' && l[d] <= '9') ++d;
        if(d > 0 && d + 1 < l.size() && (l[d] == '.' || l[d] == ')') && l[d + 1] == ' ')
            return std::string(l.substr(0, d)) + "\\" + std::string(l.substr(d));
        return std::string(l);
    }

    // ---- raw text ----

    static void raw_into(const HNode& n, std::string& o, bool pre) {
        for(const auto& k : n.kids) {
            if(k->is_text()) {
                o += k->text;
            } else if(k->tag == "br") {
                o += pre ? '\n' : ' ';
            } else if(!is_skipped(k->tag)) {
                raw_into(*k, o, pre);
                if(pre && in(k->tag, {"div", "p", "li"}) && !o.empty() && o.back() != '\n')
                    o += '\n';
            }
        }
    }
    static std::string raw_text(const HNode& n, bool pre) {
        std::string o;
        raw_into(n, o, pre);
        return o;
    }

    // ---- inline ----

    void inline_children(const HNode& n, std::string& out, const IState& st) {
        if(st.depth > 150) {
            append_text(out, raw_text(n, false));
            return;
        }
        IState s = st;
        s.depth++;
        // cppreference "t-lines": sibling spans that are one per line on the
        // page. Flattened they would run together (operator==operator!=...).
        const std::string* cls = n.attr("class");
        const bool lines = cls && (" " + *cls + " ").find(" t-lines ") != std::string::npos;
        bool any = false;
        for(const auto& k : n.kids) {
            if(k->is_text()) {
                append_text(out, k->text);
                continue;
            }
            if(lines && !is_skipped(k->tag)) {
                if(any) {
                    trim_trailing_space(out);
                    out += ", ";
                }
                any = true;
            }
            inline_elem(*k, out, s);
        }
    }

    static void space(std::string& out) {
        if(!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
    }

    // markers around `inner`, with its edge spaces kept outside them
    static void wrap(std::string& out, std::string inner, std::string_view m) {
        const bool lead = !inner.empty() && inner.front() == ' ';
        const bool trail = inner.size() > 1 && inner.back() == ' ';
        const std::string core = trim_copy(inner);
        if(core.empty()) {
            if(lead || trail) space(out);
            return;
        }
        if(lead) space(out);
        out += m;
        out += core;
        out += m;
        if(trail) out += ' ';
    }

    void inline_elem(const HNode& n, std::string& out, const IState& st) {
        const std::string& t = n.tag;
        if(is_skipped(t)) return;
        if(t == "br") {
            if(st.flat) {
                space(out);
            } else {
                trim_trailing_space(out);
                out += '\n';
            }
            return;
        }
        if(t == "wbr") return;
        if(t == "code" || t == "tt" || t == "kbd" || t == "samp") {
            std::string r;
            append_raw_collapsed(r, raw_text(n, false));
            if(r.empty()) return;
            if(trim(r).empty()) {
                space(out);
                return;
            }
            space_if_lead(out, r);
            out += code_span(trim(r));
            if(r.back() == ' ') out += ' ';
            return;
        }
        if(t == "a") return link_elem(n, out, st);
        if((t == "strong" || t == "b") && !st.strong) {
            IState s = st;
            s.strong = true;
            std::string inner;
            inline_children(n, inner, s);
            wrap(out, std::move(inner), "**");
            return;
        }
        if((t == "em" || t == "i" || t == "cite" || t == "dfn") && !st.em) {
            IState s = st;
            s.em = true;
            std::string inner;
            inline_children(n, inner, s);
            wrap(out, std::move(inner), "*");
            return;
        }
        if(t == "s" || t == "strike" || t == "del") {
            std::string inner;
            inline_children(n, inner, st);
            wrap(out, std::move(inner), "~~");
            return;
        }
        if(t == "img") {
            if(const std::string* alt = n.attr("alt"); alt && !trim(*alt).empty())
                append_text(out, *alt);
            return;
        }
        if(is_block(t)) {  // block element met in inline context (cells)
            space(out);
            inline_children(n, out, st);
            space(out);
            return;
        }
        inline_children(n, out, st);
    }

    static void append_raw_collapsed(std::string& o, std::string_view t) {
        bool sp = false;
        for(const char c : t) {
            if(is_ws(c)) {
                sp = true;
            } else {
                if(sp && !o.empty()) o += ' ';
                if(sp && o.empty()) o += ' ';
                sp = false;
                o += c;
            }
        }
        if(sp && !o.empty()) o += ' ';
    }
    static void space_if_lead(std::string& out, const std::string& r) {
        if(!r.empty() && r.front() == ' ') space(out);
    }

    void link_elem(const HNode& n, std::string& out, const IState& st) {
        IState s = st;
        s.link = true;
        std::string inner;
        inline_children(n, inner, s);
        const std::string* href = n.attr("href");
        const bool lead = !inner.empty() && inner.front() == ' ';
        const bool trail = inner.size() > 1 && inner.back() == ' ';
        std::string text = trim_copy(inner);
        std::replace(text.begin(), text.end(), '\n', ' ');
        if(text.empty()) return;
        std::string url;
        if(href && !st.link) {
            const Href h = resolve_href(*href, cx);
            switch(h.kind) {
                case Href::External: url = md_url(h.url); break;
                case Href::Frag: url = "#" + pct_encode(h.frag); break;
                case Href::Page: {
                    const std::string from = file_key(cx.page), to = file_key(h.key);
                    if(from.empty() || to.empty()) break;
                    url = pct_encode(rel_path(from, to)) + ".md";
                    if(!h.frag.empty()) url += "#" + pct_encode(h.frag);
                    break;
                }
                case Href::None: break;
            }
        }
        if(lead) space(out);
        if(url.empty()) out += text;
        else out += "[" + text + "](" + url + ")";
        if(trail) out += ' ';
    }

    static std::string md_url(std::string_view u) {
        std::string o;
        for(const char c : u) {
            if(c == ' ') o += "%20";
            else if(c == '(') o += "%28";
            else if(c == ')') o += "%29";
            else o += c;
        }
        return o;
    }

    std::string flat_inline(const HNode& n) {
        std::string o;
        IState s;
        s.flat = true;
        inline_children(n, o, s);
        std::replace(o.begin(), o.end(), '\n', ' ');
        return trim_copy(o);
    }

    // `{#id}` when the converter cannot recover the id from the text
    std::string id_tag(const HNode& n, std::string_view text) {
        std::string id;
        find_id(n, id, 0);
        if(id.empty()) return {};
        if(slugify(id) == slugify(text)) return {};
        return " {#" + id + "}";
    }
    static void find_id(const HNode& n, std::string& id, int depth) {
        if(!id.empty() || depth > 4) return;
        if(const std::string* v = n.attr("id"); v && !v->empty()) {
            id = *v;
            return;
        }
        if(n.tag == "a")
            if(const std::string* v = n.attr("name"); v && !v->empty()) {
                id = *v;
                return;
            }
        for(const auto& k : n.kids)
            if(!k->is_text()) find_id(*k, id, depth + 1);
    }

    // ---- blocks ----

    void block_elem(const HNode& k, std::vector<Blk>& out, int depth) {
        const std::string& t = k.tag;
        if(t.size() == 2 && t[0] == 'h' && t[1] >= '1' && t[1] <= '6') {
            const std::string text = flat_inline(k);
            if(text.empty()) return;
            out.push_back({Blk::Other, std::string(static_cast<size_t>(t[1] - '0'), '#') +
                                           " " + text + id_tag(k, text)});
        } else if(t == "pre") {
            fenced(k, out);
        } else if(t == "ul" || t == "ol" || t == "menu" || t == "dir") {
            list(k, out, depth);
        } else if(t == "dl") {
            dlist(k, out, depth);
        } else if(t == "table") {
            table(k, out, depth);
        } else if(t == "blockquote") {
            const std::string body = join(flow(k, depth + 1), false);
            if(body.empty()) return;
            std::string o;
            size_t from = 0;
            while(from <= body.size()) {
                size_t nl = body.find('\n', from);
                if(nl == std::string::npos) nl = body.size();
                const std::string_view line = std::string_view(body).substr(from, nl - from);
                o += line.empty() ? ">" : "> ";
                o += line;
                if(nl < body.size()) o += '\n';
                from = nl + 1;
            }
            out.push_back({Blk::Other, std::move(o)});
        } else if(t == "hr") {
            out.push_back({Blk::Other, "---"});
        } else {  // div, section, li outside a list ...: transparent
            for(Blk& b : flow(k, depth + 1)) out.push_back(std::move(b));
        }
    }

    std::string pre_lang(const HNode& n) const {
        if(const std::string* d = n.attr("data-language"); d && !d->empty())
            return fence_lang_clean(*d);
        const auto from_class = [](const HNode& x) -> std::string {
            const std::string* c = x.attr("class");
            if(!c) return {};
            for(const std::string_view tok : split(*c, ' ')) {
                for(const std::string_view p :
                    {"language-", "lang-", "source-", "highlight-"})
                    if(tok.starts_with(p) && tok.size() > p.size())
                        return fence_lang_clean(tok.substr(p.size()));
            }
            return {};
        };
        if(std::string l = from_class(n); !l.empty()) return l;
        for(const auto& k : n.kids)
            if(!k->is_text() && k->tag == "code")
                if(std::string l = from_class(*k); !l.empty()) return l;
        return fence_lang_clean(cx.fence);
    }

    void fenced(const HNode& n, std::vector<Blk>& out) {
        std::string text = raw_text(n, true);
        std::string t;
        for(size_t i = 0; i < text.size(); ++i) {  // CRLF -> LF
            if(text[i] == '\r') {
                if(i + 1 < text.size() && text[i + 1] == '\n') continue;
                t += '\n';
            } else {
                t += text[i];
            }
        }
        while(!t.empty() && t.front() == '\n') t.erase(t.begin());
        while(!t.empty() && (t.back() == '\n' || is_ws(t.back()))) t.pop_back();
        if(t.empty()) return;
        size_t run = 0, best = 0;
        for(const char c : t) {
            run = c == '`' ? run + 1 : 0;
            best = std::max(best, run);
        }
        const std::string fence(std::max<size_t>(3, best + 1), '`');
        out.push_back({Blk::Other, fence + pre_lang(n) + "\n" + t + "\n" + fence});
    }

    std::string li_body(const HNode& li, int depth) {
        return join(flow(li, depth + 1), true);
    }

    std::string render_list(const HNode& n, int depth) {
        const bool ordered = n.tag == "ol";
        long idx = 1;
        if(const std::string* s = n.attr("start"))
            idx = std::strtol(s->c_str(), nullptr, 10);
        std::vector<std::string> items;
        bool loose = false;
        const auto add = [&](std::string body) {
            if(body.empty()) return;
            const std::string mk = ordered ? std::to_string(idx++) + ". " : "- ";
            std::string item = mk;
            const std::string pad(mk.size(), ' ');
            size_t from = 0;
            bool first = true;
            while(from <= body.size()) {
                size_t nl = body.find('\n', from);
                if(nl == std::string::npos) nl = body.size();
                const std::string_view line = std::string_view(body).substr(from, nl - from);
                if(!first) item += '\n';
                if(!first && !line.empty()) item += pad;
                item += line;
                first = false;
                from = nl + 1;
            }
            if(item.find("\n\n") != std::string::npos) loose = true;
            items.push_back(std::move(item));
        };
        for(const auto& kp : n.kids) {
            const HNode& k = *kp;
            if(k.is_text()) {
                std::string t;
                append_text(t, k.text);
                if(!trim(t).empty()) add(trim_copy(t));
            } else if(k.tag == "li") {
                add(li_body(k, depth));
            } else if((k.tag == "ul" || k.tag == "ol") && !items.empty()) {
                items.back() += "\n" + indent(render_list(k, depth + 1), 2);
            } else if(!is_skipped(k.tag)) {
                add(join(flow(*std::make_unique<HNode>(wrap_node(k)), depth + 1), true));
            }
        }
        std::string o;
        for(size_t i = 0; i < items.size(); ++i) {
            if(i) o += loose ? "\n\n" : "\n";
            o += items[i];
        }
        return o;
    }

    // a one-child holder, so flow() can treat a stray element as content
    static HNode wrap_node(const HNode& k) {
        HNode h;
        h.tag = "li";
        auto c = std::make_unique<HNode>();
        c->tag = k.tag;
        c->attrs = k.attrs;
        for(const auto& kk : k.kids) {
            auto copy = std::make_unique<HNode>();
            copy_node(*kk, *copy);
            c->kids.push_back(std::move(copy));
        }
        h.kids.push_back(std::move(c));
        return h;
    }
    static void copy_node(const HNode& a, HNode& b) {
        b.tag = a.tag;
        b.text = a.text;
        b.attrs = a.attrs;
        for(const auto& k : a.kids) {
            auto c = std::make_unique<HNode>();
            copy_node(*k, *c);
            c->parent = &b;
            b.kids.push_back(std::move(c));
        }
    }

    void list(const HNode& n, std::vector<Blk>& out, int depth) {
        std::string s = render_list(n, depth);
        if(!s.empty()) out.push_back({Blk::List, std::move(s)});
    }

    void dlist(const HNode& n, std::vector<Blk>& out, int depth) {
        std::vector<std::string> groups;
        std::string cur;
        bool have_dd = false;
        const auto walk = [&](auto&& self, const HNode& p) -> void {
            for(const auto& kp : p.kids) {
                const HNode& k = *kp;
                if(k.is_text() || is_skipped(k.tag)) continue;
                if(k.tag == "dt") {
                    if(have_dd) {
                        groups.push_back(std::move(cur));
                        cur.clear();
                        have_dd = false;
                    }
                    const std::string term = flat_inline(k);
                    if(term.empty()) continue;
                    if(!cur.empty()) cur += '\n';
                    cur += "**" + term + "**" + id_tag(k, term);
                } else if(k.tag == "dd") {
                    const std::string body = join(flow(k, depth + 1), false);
                    if(body.empty()) continue;
                    if(!cur.empty()) cur += '\n';
                    cur += indent(body, 2);
                    have_dd = true;
                } else {
                    self(self, k);
                }
            }
        };
        walk(walk, n);
        if(!cur.empty()) groups.push_back(std::move(cur));
        std::string o;
        for(size_t i = 0; i < groups.size(); ++i) {
            if(i) o += "\n\n";
            o += groups[i];
        }
        if(!o.empty()) out.push_back({Blk::Other, std::move(o)});
    }

    // ---- tables ----

    struct Cell {
        std::string text;
        bool th = false, rich = false;
        int span = 1;
    };
    struct Row {
        std::vector<Cell> cells;
        bool head = false;
    };

    void collect_rows(const HNode& n, std::vector<Row>& rows, bool head, std::string& caption, int depth) {
        for(const auto& kp : n.kids) {
            const HNode& k = *kp;
            if(k.is_text()) continue;
            if(k.tag == "tr") {
                Row r;
                r.head = head;
                for(const auto& cp : k.kids) {
                    if(cp->is_text() || (cp->tag != "td" && cp->tag != "th")) continue;
                    Cell c;
                    c.th = cp->tag == "th";
                    c.rich = has_rich_cell(*cp);
                    c.text = flat_inline(*cp);
                    if(const std::string* s = cp->attr("colspan"))
                        c.span = std::clamp(static_cast<int>(std::strtol(s->c_str(), nullptr, 10)), 1, 30);
                    r.cells.push_back(std::move(c));
                }
                if(!r.cells.empty()) rows.push_back(std::move(r));
            } else if(k.tag == "thead") {
                collect_rows(k, rows, true, caption, depth + 1);
            } else if(k.tag == "tbody" || k.tag == "tfoot") {
                collect_rows(k, rows, false, caption, depth + 1);
            } else if(k.tag == "caption") {
                caption = flat_inline(k);
            }
        }
    }

    static std::string cell_md(std::string s) {
        std::string o;
        for(const char c : s) {
            if(c == '|') o += "\\|";
            else o += c;
        }
        return o;
    }

    static constexpr int kWideCols = 3;  // more columns than this: a list

    void table(const HNode& n, std::vector<Blk>& out, int depth) {
        std::vector<Row> rows;
        std::string caption;
        collect_rows(n, rows, false, caption, depth);
        if(rows.empty()) return;
        int cols = 0;
        bool wide = false;
        for(const Row& r : rows) {
            int c = 0;
            for(const Cell& x : r.cells) {
                c += x.span;
                if(x.rich || x.span > 1) wide = true;
            }
            cols = std::max(cols, c);
        }
        wide = wide || cols > kWideCols;
        std::string o;
        if(!caption.empty()) out.push_back({Blk::Other, "**" + caption + "**"});

        const bool has_head = rows[0].head ||
                              std::all_of(rows[0].cells.begin(), rows[0].cells.end(),
                                          [](const Cell& c) { return c.th; });
        // cells holding only a dash are separators (cppreference parameter
        // tables are "name | - | description"): never content
        const auto is_dash = [](const std::string& t) {
            const std::string_view v = trim(t);
            return v == "-" || v == "\xe2\x80\x94" || v == "\xe2\x80\x93";
        };
        if(!wide && !has_head)  // a parameter table: never a pipe table
            for(const Row& r : rows)
                for(const Cell& c : r.cells)
                    if(is_dash(c.text)) wide = true;
        if(!wide) {
            const auto line = [&](const Row& r) {
                std::string l = "|";
                for(int i = 0; i < cols; ++i) {
                    const std::string t = i < static_cast<int>(r.cells.size())
                                              ? cell_md(r.cells[static_cast<size_t>(i)].text)
                                              : std::string();
                    l += " " + t + " |";
                }
                return l;
            };
            o += line(rows[0]) + "\n|";
            for(int i = 0; i < cols; ++i) o += " --- |";
            for(size_t i = 1; i < rows.size(); ++i) o += "\n" + line(rows[i]);
            out.push_back({Blk::Other, std::move(o)});
            return;
        }
        // wide: one bullet per row, the other cells as sub-bullets
        std::vector<std::string> head;
        size_t first = 0;
        if(has_head) {
            for(const Cell& c : rows[0].cells) head.push_back(c.text);
            first = 1;
        }
        const auto content_cells = [&](const Row& r) {
            std::vector<size_t> idx;
            for(size_t i = 0; i < r.cells.size(); ++i)
                if(!r.cells[i].text.empty() && !is_dash(r.cells[i].text)) idx.push_back(i);
            return idx;
        };
        // every row a "name, description" pair under no header labels: render
        // like a definition list, **name** with the description indented
        {
            bool pairs = true;
            size_t n = 0;
            for(size_t ri = first; ri < rows.size() && pairs; ++ri) {
                const std::vector<size_t> idx = content_cells(rows[ri]);
                if(idx.empty()) continue;
                const bool labelled = idx.size() > 1 && idx[1] < head.size() &&
                                      !head[idx[1]].empty();
                if(idx.size() != 2 || labelled || rows[ri].cells[idx[0]].span > 1 ||
                   rows[ri].cells[idx[1]].span > 1)
                    pairs = false;
                ++n;
            }
            if(pairs && n > 0) {
                std::string dl;
                for(size_t ri = first; ri < rows.size(); ++ri) {
                    const Row& r = rows[ri];
                    const std::vector<size_t> idx = content_cells(r);
                    if(idx.empty()) continue;
                    if(!dl.empty()) dl += "\n\n";
                    dl += "**" + r.cells[idx[0]].text + "**\n  " + r.cells[idx[1]].text;
                }
                out.push_back({Blk::Other, std::move(dl)});
                return;
            }
        }
        std::string body;
        bool last_bold = false;
        for(size_t ri = first; ri < rows.size(); ++ri) {
            const Row& r = rows[ri];
            const std::vector<size_t> idx = content_cells(r);
            if(idx.empty()) continue;
            if(idx.size() == 1 && r.cells[idx[0]].span > 1) {
                if(!body.empty()) body += "\n\n";
                body += "**" + r.cells[idx[0]].text + "**\n";
                last_bold = true;
                continue;
            }
            if(!body.empty()) body += last_bold ? "\n" : "\n";
            last_bold = false;
            body += "- " + r.cells[idx[0]].text;
            for(size_t k = 1; k < idx.size(); ++k) {
                const std::string label = idx[k] < head.size() ? head[idx[k]] : std::string();
                body += "\n  - ";
                if(!label.empty()) body += label + ": ";
                body += r.cells[idx[k]].text;
            }
        }
        if(!body.empty()) out.push_back({Blk::List, std::move(body)});
    }
};

// normalise blank lines outside code fences; end with a single newline
std::string finalize(std::string md) {
    std::string o;
    o.reserve(md.size());
    bool in_fence = false;
    size_t fence_len = 0;
    int blanks = 0;
    size_t from = 0;
    while(from <= md.size()) {
        size_t nl = md.find('\n', from);
        if(nl == std::string::npos) nl = md.size();
        std::string_view line = std::string_view(md).substr(from, nl - from);
        from = nl + 1;
        std::string_view tl = trim(line);
        size_t ticks = 0;
        while(ticks < tl.size() && tl[ticks] == '`') ++ticks;
        if(!in_fence && ticks >= 3) {
            in_fence = true;
            fence_len = ticks;
        } else if(in_fence && ticks >= fence_len && ticks == tl.size()) {
            in_fence = false;
        }
        if(!in_fence && tl.empty()) {
            if(++blanks > 1) continue;
            o += '\n';
            continue;
        }
        blanks = 0;
        o += line;
        o += '\n';
    }
    while(o.size() > 1 && o[o.size() - 1] == '\n' && o[o.size() - 2] == '\n')
        o.pop_back();
    while(!o.empty() && o.front() == '\n') o.erase(o.begin());
    return o;
}

// ---- saved-HTML link rewriting ---------------------------------------------

std::string html_escape(std::string_view s) {
    std::string o;
    for(const char c : s) {
        switch(c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

std::string rewrite_html_links(std::string_view h, const ConvertCtx& cx) {
    std::string o;
    o.reserve(h.size() + h.size() / 16);
    size_t i = 0;
    const size_t n = h.size();
    while(i < n) {
        if(h.compare(i, 4, "<!--") == 0) {
            const size_t e = h.find("-->", i + 4);
            const size_t stop = e == std::string_view::npos ? n : e + 3;
            o.append(h.substr(i, stop - i));
            i = stop;
            continue;
        }
        const bool at_href = i + 5 < n && (h[i] == 'h' || h[i] == 'H') &&
                             lower(h.substr(i, 5)) == "href=" && i > 0 && is_ws(h[i - 1]);
        if(!at_href) {
            o += h[i++];
            continue;
        }
        size_t j = i + 5;
        if(j < n && (h[j] == '"' || h[j] == '\'')) {
            const char q = h[j];
            const size_t e = h.find(q, j + 1);
            if(e == std::string_view::npos) {
                o += h[i++];
                continue;
            }
            const std::string val = clean_text(h.substr(j + 1, e - j - 1));
            const Href r = resolve_href(val, cx);
            o.append(h.substr(i, 5));
            o += q;
            if(r.kind == Href::Page) {
                const std::string from = file_key(cx.page), to = file_key(r.key);
                if(from.empty() || to.empty()) {
                    o += html_escape(val);
                } else {
                    o += html_escape(pct_encode(rel_path(from, to)) + ".html");
                    if(!r.frag.empty()) o += "#" + html_escape(pct_encode(r.frag));
                }
            } else {
                o += h.substr(j + 1, e - j - 1);
            }
            o += q;
            i = e + 1;
        } else {
            o += h[i++];
        }
    }
    return o;
}

}  // namespace

// ---- public ---------------------------------------------------------------

std::string md_escape(std::string_view s) {
    std::string o;
    esc_into(o, s);
    return o;
}

std::string html_escape_text(std::string_view s) { return html_escape(s); }

std::string pct_encode(std::string_view s) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string o;
    for(const char c : s) {
        if(is_alnum(c) || c == '/' || c == '.' || c == '_' || c == '~' || c == '-') {
            o += c;
        } else {
            const auto u = static_cast<unsigned char>(c);
            o += '%';
            o += hex[u >> 4];
            o += hex[u & 15];
        }
    }
    return o;
}

std::string pct_decode(std::string_view s) {
    const auto hv = [](char c) -> int {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string o;
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == '%' && i + 2 < s.size() && hv(s[i + 1]) >= 0 && hv(s[i + 2]) >= 0) {
            o += static_cast<char>(hv(s[i + 1]) * 16 + hv(s[i + 2]));
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::string file_key(std::string_view key) {
    if(key.find('\0') != std::string_view::npos) return {};
    while(!key.empty() && key.front() == '/') key.remove_prefix(1);
    std::string k(key);
    if(k.empty()) return {};
    if(k.back() == '/') k += "index";
    std::string o;
    bool first = true;
    for(std::string_view seg : split(k, '/')) {
        if(seg.empty() || seg == "." || seg == ".." || seg.size() > 200) return {};
        std::string s(seg);
        for(char& c : s)
            if(static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = '_';
        if(first && s == "html") s = "_html";
        if(!first) o += '/';
        o += s;
        first = false;
    }
    if(o == "INDEX") o = "_INDEX";
    return o;
}

std::string html_to_md(std::string_view html, const ConvertCtx& cx) {
    try {
        const auto root = parse_html(html);
        Conv c(cx);
        return finalize(Conv::join(c.flow(*root, 0), false));
    } catch(...) {
        return {};
    }
}

std::string html_to_page(std::string_view html, std::string_view title,
                         const ConvertCtx& cx) {
    std::string o =
        "<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>";
    o += html_escape(title);
    o += "</title></head><body>\n";
    o += rewrite_html_links(html, cx);
    o += "\n</body></html>\n";
    return o;
}

// ---- anchors --------------------------------------------------------------

namespace {

// a heading / term line reduced to the words a reader sees
std::string plain_line(std::string_view l) {
    l = trim(l);
    while(!l.empty() && l.front() == '#') l.remove_prefix(1);
    if(const size_t b = l.rfind(" {#"); b != std::string_view::npos && l.ends_with('}'))
        l = l.substr(0, b);
    std::string o;
    for(size_t i = 0; i < l.size(); ++i) {
        const char c = l[i];
        if(c == '*' || c == '`' || c == '\\') continue;
        if(c == ']' && i + 1 < l.size() && l[i + 1] == '(') {  // [text](url)
            const size_t e = l.find(')', i);
            if(e != std::string_view::npos) i = e;
            continue;
        }
        if(c == '[') continue;
        o += c;
    }
    return o;
}

}  // namespace

int find_anchor(std::string_view md, std::string_view anchor) {
    if(anchor.empty()) return -1;
    const std::string want = pct_decode(anchor);
    const std::string tag = " {#" + want + "}";
    const std::string ws = slugify(want);
    int by_slug = -1, line_no = 0;
    bool fenced = false;
    size_t from = 0;
    while(from <= md.size()) {
        size_t nl = md.find('\n', from);
        if(nl == std::string_view::npos) nl = md.size();
        const std::string_view line = md.substr(from, nl - from);
        from = nl + 1;
        const int here = line_no++;
        if(line.starts_with("```")) {
            fenced = !fenced;
            continue;
        }
        if(fenced) continue;
        if(line.ends_with(tag)) return here;
        const bool head = line.starts_with("#");
        const bool term = line.starts_with("**");
        if(by_slug < 0 && (head || term) && !ws.empty() &&
           slugify(plain_line(line)) == ws)
            by_slug = here;
    }
    return by_slug;
}

// ---- wide tables -> lists ---------------------------------------------------
// A buffer line is one screen row, so a pipe table wider than the window can
// only be truncated. At show time such a table becomes the form the converter
// already uses for wide tables: **name** with the description indented under
// it (two columns), or a bullet per row with "Header: value" sub-bullets.
// Text soft-wraps, so nothing is cut. Tables that fit are left alone.

namespace {

std::vector<std::string> split_row(std::string_view line) {
    std::string_view t = trim(line);
    if(!t.empty() && t.front() == '|') t.remove_prefix(1);
    std::vector<std::string> cells;
    std::string cur;
    bool closed = true;  // the last cell was ended by a pipe
    for(size_t i = 0; i < t.size(); ++i) {
        if(t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cur += "\\|";
            ++i;
            closed = false;
        } else if(t[i] == '|') {
            cells.emplace_back(trim(cur));
            cur.clear();
            closed = true;
        } else {
            cur += t[i];
            closed = false;
        }
    }
    if(!closed) cells.emplace_back(trim(cur));
    return cells;
}

bool is_delim_row(std::string_view line) {
    const std::string_view t = trim(line);
    if(t.size() < 3 || t.front() != '|') return false;
    bool dash = false;
    for(const char c : t) {
        if(c == '-') dash = true;
        else if(c != '|' && c != ':' && c != ' ') return false;
    }
    return dash;
}

// Columns a cell takes once link targets and inline markers are not drawn.
int shown_width(std::string_view s) {
    int w = 0;
    for(size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if(c == '\\' || c == '`' || c == '*') continue;
        if(c == '[') continue;
        if(c == ']' && i + 1 < s.size() && s[i + 1] == '(') {
            int d = 0;
            size_t j = i + 1;
            for(; j < s.size(); ++j) {
                if(s[j] == '(') ++d;
                else if(s[j] == ')' && --d == 0) break;
            }
            i = j;
            continue;
        }
        if((c & 0xC0) != 0x80) ++w;  // one per code point
    }
    return w;
}

}  // namespace

std::string reflow_tables(std::string_view md, int width) {
    std::vector<std::string_view> lines;
    for(size_t from = 0; from <= md.size();) {
        size_t nl = md.find('\n', from);
        if(nl == std::string_view::npos) nl = md.size();
        lines.push_back(md.substr(from, nl - from));
        from = nl + 1;
    }
    std::string o;
    o.reserve(md.size());
    bool fenced = false;
    for(size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        const std::string_view tl = trim(line);
        const bool last_line = i + 1 == lines.size();
        const auto emit = [&](std::string_view t) {
            o += t;
            if(!last_line) o += '\n';
        };
        if(tl.starts_with("```") || tl.starts_with("~~~")) fenced = !fenced;
        if(fenced || line.empty() || line.front() != '|' || i + 1 >= lines.size() ||
           !is_delim_row(lines[i + 1])) {
            emit(line);
            continue;
        }
        size_t end = i;  // last line of the table
        while(end + 1 < lines.size() && !lines[end + 1].empty() && lines[end + 1].front() == '|')
            ++end;
        std::vector<std::vector<std::string>> rows;
        for(size_t k = i; k <= end; ++k)
            if(k != i + 1) rows.push_back(split_row(lines[k]));
        size_t cols = 0;
        for(const auto& r : rows) cols = std::max(cols, r.size());
        std::vector<int> cw(cols, 0);
        for(const auto& r : rows)
            for(size_t c = 0; c < r.size(); ++c) cw[c] = std::max(cw[c], shown_width(r[c]));
        int total = 1;
        for(const int w : cw) total += w + 3;
        if(total <= width || cols < 2) {
            for(size_t k = i; k <= end; ++k) {
                o += lines[k];
                if(!(k == end && last_line)) o += '\n';
            }
            i = end;
            continue;
        }
        // The converter writes a headerless table with its first row as the
        // header, so a first row only counts as one when it looks like labels:
        // short, plain cells (data rows carry links and code).
        bool has_head = true;
        for(const std::string& c : rows[0])
            if(shown_width(c) > 24 || c.find("](") != std::string::npos ||
               c.find('`') != std::string::npos)
                has_head = false;
        static const std::vector<std::string> kNoHead;
        const std::vector<std::string>& head = has_head ? rows[0] : kNoHead;
        const size_t body0 = has_head ? 1 : 0;
        std::string out;
        const auto bold = [](const std::string& s) {
            return s.starts_with("**") ? s : "**" + s + "**";
        };
        if(cols == 2) {
            const bool labels = head.size() == 2 && !head[0].empty() && !head[1].empty();
            if(labels) out += "*" + head[0] + " — " + head[1] + "*\n\n";
            bool first = true;
            for(size_t r = body0; r < rows.size(); ++r) {
                const auto& row = rows[r];
                if(row.empty() || row[0].empty()) continue;
                if(!first) out += "\n\n";
                first = false;
                out += bold(row[0]);
                if(row.size() > 1 && !row[1].empty()) out += "\n  " + row[1];
            }
        } else {
            bool first = true;
            for(size_t r = body0; r < rows.size(); ++r) {
                const auto& row = rows[r];
                if(row.empty() || row[0].empty()) continue;
                if(!first) out += "\n";
                first = false;
                out += "- " + row[0];
                for(size_t c = 1; c < row.size(); ++c) {
                    if(row[c].empty()) continue;
                    out += "\n  - ";
                    if(c < head.size() && !head[c].empty()) out += head[c] + ": ";
                    out += row[c];
                }
            }
        }
        o += out;
        if(!(end == lines.size() - 1)) o += '\n';
        i = end;
    }
    return o;
}

// ---- semantic line breaks ---------------------------------------------------
// One sentence per source line (and a break after each trailing version group
// such as "(since C++17)"), applied to a converted page when it is shown. The
// text between breaks is never changed, so rendering is unaffected apart from
// where the lines end. Headings, tables, fences, rules and term lines are left
// alone; code spans, link labels/targets, `**bold**` and parentheses are never
// split.

namespace {

// "(since C++17)", "(until C++11)", "(removed in C++20)", ... starting at
// s[i] == '('. Returns the index after the ')', or 0 when s[i..] is not one.
size_t version_group_end(std::string_view s, size_t i) {
    static constexpr std::string_view kLead[] = {
        "(since ", "(until ", "(removed in ", "(removed ", "(deprecated in ",
        "(deprecated ", "(constexpr since ", "(optional)", "(TM)"};
    if(i >= s.size() || s[i] != '(') return 0;
    bool ok = false;
    for(const std::string_view k : kLead)
        if(s.compare(i, k.size(), k) == 0) {
            ok = true;
            break;
        }
    if(!ok) return 0;
    const size_t close = s.find(')', i);
    if(close == std::string_view::npos || close - i > 64) return 0;
    const std::string_view in = s.substr(i, close - i);
    if(in.find_first_of("[]`(") != std::string_view::npos && in.find('(', 1) != std::string_view::npos)
        return 0;
    if(in.find_first_of("[]`") != std::string_view::npos) return 0;
    return close + 1;
}

bool is_abbrev(std::string_view s, size_t dot) {
    static const std::set<std::string, std::less<>> kAbbr = {
        "e.g", "i.e", "etc", "vs", "cf", "approx", "resp", "esp", "incl",
        "viz", "fig", "no", "vol", "sec", "ex", "eg", "ie", "al", "ca", "est"};
    size_t b = dot;
    while(b > 0 && (std::isalpha(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '.'))
        --b;
    std::string tok(s.substr(b, dot - b));
    for(char& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(kAbbr.contains(tok)) return true;
    // an initial ("A. Stepanov")
    return tok.size() == 1 && std::isupper(static_cast<unsigned char>(s[b]));
}

// Length of a "[label](target)" starting at s[i] == '[' (0 = not a link).
size_t link_len(std::string_view s, size_t i) {
    int depth = 0;
    size_t j = i;
    for(; j < s.size(); ++j) {
        if(s[j] == '\\') {
            ++j;
            continue;
        }
        if(s[j] == '[') ++depth;
        else if(s[j] == ']' && --depth == 0) break;
    }
    if(j + 1 >= s.size() || s[j] != ']' || s[j + 1] != '(') return 0;
    int pd = 0;
    for(size_t k = j + 1; k < s.size(); ++k) {
        if(s[k] == '\\') {
            ++k;
            continue;
        }
        if(s[k] == '(') ++pd;
        else if(s[k] == ')' && --pd == 0) return k + 1 - i;
    }
    return 0;
}

// Break offsets (after which a new line starts) inside one paragraph line.
std::vector<size_t> break_points(std::string_view s) {
    std::vector<size_t> out;
    bool bold = false;
    int paren = 0, run_text = -1;
    size_t seg = 0;  // start of the current piece
    const auto has_text = [&](size_t a, size_t b) {
        return s.substr(a, b - a).find_first_not_of(' ') != std::string_view::npos;
    };
    size_t i = 0;
    while(i < s.size()) {
        const char c = s[i];
        if(c == '\\') {
            i += 2;
            continue;
        }
        if(c == '`') {  // code span: same-length closing run
            size_t n = 0;
            while(i + n < s.size() && s[i + n] == '`') ++n;
            const std::string close(n, '`');
            const size_t e = s.find(close, i + n);
            i = e == std::string_view::npos ? i + n : e + n;
            continue;
        }
        if(c == '[') {
            if(const size_t n = link_len(s, i)) {
                i += n;
                continue;
            }
        }
        if(c == '*' && i + 1 < s.size() && s[i + 1] == '*') {
            bold = !bold;
            i += 2;
            continue;
        }
        if(c == '(') {
            if(const size_t e = version_group_end(s, i)) {
                const bool had = has_text(seg, i);
                if(run_text < 0) run_text = had ? 1 : 0;
                size_t n = e;
                while(n < s.size() && s[n] == ' ') ++n;
                if(n < s.size() && version_group_end(s, n)) {  // run of groups
                    i = n;
                    continue;
                }
                if(run_text == 1 && n < s.size() && !bold && paren == 0) {
                    out.push_back(e);
                    seg = n;
                }
                run_text = -1;
                i = n;
                continue;
            }
            ++paren;
        } else if(c == ')') {
            if(paren > 0) --paren;
        } else if((c == '.' || c == '?' || c == '!') && !bold && paren == 0) {
            size_t e = i + 1;
            while(e < s.size() && (s[e] == ')' || s[e] == '"' || s[e] == '\'' || s[e] == '*' || s[e] == '_'))
                ++e;
            size_t n = e;
            while(n < s.size() && s[n] == ' ') ++n;
            if(n > e && n < s.size() && !(c == '.' && is_abbrev(s, i))) {
                const char nx = s[n];
                const bool starts =
                    std::isupper(static_cast<unsigned char>(nx)) || nx == '[' ||
                    nx == '`' || nx == '*' || nx == '"' ||
                    (nx == '(' && !version_group_end(s, n));
                if(starts && has_text(seg, i)) {
                    out.push_back(e);
                    seg = n;
                    i = n;
                    continue;
                }
            }
        }
        ++i;
    }
    return out;
}

}  // namespace

std::string semantic_breaks(std::string_view md) {
    std::string o;
    o.reserve(md.size() + md.size() / 8);
    bool fenced = false;
    size_t from = 0;
    while(from <= md.size()) {
        size_t nl = md.find('\n', from);
        if(nl == std::string_view::npos) nl = md.size();
        const std::string_view line = md.substr(from, nl - from);
        const bool last = nl == md.size();
        from = nl + 1;
        const auto emit = [&](std::string_view t) {
            o += t;
            if(!last) o += '\n';
        };
        const std::string_view tl = trim(line);
        if(tl.starts_with("```") || tl.starts_with("~~~")) {
            fenced = !fenced;
            emit(line);
            continue;
        }
        if(fenced || tl.empty() || tl.front() == '#' || tl.front() == '|' ||
           tl.starts_with("---") || tl.starts_with("***") || tl.starts_with("<") ||
           line.find(" {#") != std::string_view::npos) {
            emit(line);
            continue;
        }
        // prefix: indent, quote marks, list marker
        size_t p = 0;
        while(p < line.size() && line[p] == ' ') ++p;
        const size_t indent = p;
        size_t q = p;
        std::string quote;
        while(q < line.size() && line[q] == '>') {
            ++q;
            if(q < line.size() && line[q] == ' ') ++q;
        }
        quote = std::string(line.substr(p, q - p));
        p = q;
        size_t marker = 0;
        if(p + 1 < line.size() && (line[p] == '-' || line[p] == '*' || line[p] == '+') &&
           line[p + 1] == ' ') {
            marker = 2;
        } else {
            size_t d = p;
            while(d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
            if(d > p && d + 1 < line.size() && (line[d] == '.' || line[d] == ')') && line[d + 1] == ' ')
                marker = d + 2 - p;
        }
        const std::string_view body = line.substr(p + marker);
        const std::vector<size_t> cuts = break_points(body);
        if(cuts.empty()) {
            emit(line);
            continue;
        }
        const std::string cont = quote.empty() ? std::string(indent + marker, ' ')
                                               : std::string(line.substr(0, p)) + std::string(marker, ' ');
        o += line.substr(0, p + marker);
        size_t at = 0;
        for(const size_t c : cuts) {
            std::string_view piece = body.substr(at, c - at);
            while(!piece.empty() && piece.back() == ' ') piece.remove_suffix(1);
            o += piece;
            o += '\n';
            o += cont;
            at = c;
            while(at < body.size() && body[at] == ' ') ++at;
        }
        emit(body.substr(at));
    }
    return o;
}

}  // namespace doc
