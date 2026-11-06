#include <cctype>
#include <memory>
#include <string_view>
#include "columns.hpp"
#include "indent_model.hpp"

namespace indent {
namespace {

bool word_char(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

HeaderPtr push_header(const HeaderPtr& p, int ind, HKind k) {
    return std::make_shared<const Header>(Header{p, ind, k});
}

struct Walker {
    LineState& s;
    std::string_view raw, code;
    int ind;
    uint32_t line;
    size_t i = 0;

    Stmt& st() { return s.stmt; }
    Kind top_kind() const { return s.frames ? s.frames->kind : Kind::Block; }
    bool in_group() const {
        return s.frames && (s.frames->kind == Kind::Paren ||
                            s.frames->kind == Kind::Bracket);
    }
    bool at_start() const {
        return s.stmt.pending == Pending::None ||
               s.stmt.pending == Pending::Label ||
               s.stmt.pending == Pending::Header;
    }

    void note_first() {
        if(!s.frames || s.frames->open != '{' || s.frames->first_ind >= 0)
            return;
        Frame f = *s.frames;
        f.first_ind = ind;
        s.frames = std::make_shared<const Frame>(std::move(f));
    }

    void begin_stmt() {
        if(!at_start()) return;
        note_first();
        if(st().pending != Pending::Header) st().headers = nullptr;
        st().pending = Pending::Cont;
        st().kw = Kw::None;
        st().paren_seen = st().init_list = false;
        st().angle = st().tern = 0, st().rhs_col = -1;
        st().after_hdr = false;
        st().last = 0;
        st().line = line;
        st().ind = ind;
    }

    void push(Kind k, char open, int align, bool hp, int owner) {
        const Stmt outer = st();
        s.frames = std::make_shared<const Frame>(
            Frame{s.frames, k, open, line, owner, align, hp, outer});
        st() = Stmt{};
    }

    bool pop_to(char open) {
        for(FramePtr f = s.frames; f; f = f->parent)
            if(f->open == open) return true;
        return false;
    }

    void close(char open) {
        if(!pop_to(open)) return;  // stray closer: ignore it
        while(s.frames->open != open) s.frames = s.frames->parent;
        const Frame f = *s.frames;
        s.frames = f.parent;
        st() = f.outer;
        st().last = open == '{' ? '}' : (open == '(' ? ')' : ']');
        if(open == '{') {
            const bool cont = f.kind == Kind::Init || f.kind == Kind::Class;
            st().pending = cont ? Pending::Cont : Pending::None;
            if(!cont) st().kw = Kw::None;
            st().paren_seen = st().init_list = false;
            st().angle = st().tern = 0, st().rhs_col = -1;
        } else {
            st().pending = f.header_paren ? Pending::Header : Pending::Cont;
            if(open == '(') st().paren_seen = true;
        }
    }

    Kind brace_kind(bool header_body, bool first_arg) {
        const Stmt& t = st();
        if(t.kw == Kw::Switch) return Kind::Switch;
        if(t.kw == Kw::Scope) return Kind::Scope;
        if(header_body) return Kind::Block;
        if(first_arg) return Kind::Init;  // f({...}), a[{...}]
        if(t.init_list && t.last == 'w') return Kind::Init;
        if(t.paren_seen) return Kind::Block;
        if(top_kind() == Kind::Init) return Kind::Init;
        if(t.last == '=' || t.last == ',' || t.last == '(' || t.last == '[' ||
           t.last == '>')
            return Kind::Init;
        if(t.kw == Kw::Enum) return Kind::Init;
        if(t.kw == Kw::Class) return Kind::Class;
        return t.last == 'w' ? Kind::Init : Kind::Block;
    }

    int align_after(size_t k) const {
        const size_t n = code.find_first_not_of(" \t", k + 1);
        if(n == std::string_view::npos) return -1;
        return cx_to_rx(raw, static_cast<int>(n));
    }

    bool try_label(std::string_view w, size_t end) {
        const bool cs = w == "case", df = w == "default";
        const bool acc =
            (w == "public" || w == "private" || w == "protected") &&
            top_kind() == Kind::Class;
        if(!cs && !df && !acc) return false;
        size_t k = end;
        if(cs) {
            int depth = 0;
            for(; k < code.size(); ++k) {
                const char c = code[k];
                if(c == '(' || c == '[') ++depth;
                else if(c == ')' || c == ']') --depth;
                else if(c == ':' && depth <= 0) {
                    if(k + 1 < code.size() && code[k + 1] == ':') ++k;
                    else break;
                }
            }
            if(k >= code.size()) return false;
        } else {
            k = code.find_first_not_of(" \t", end);
            if(k == std::string_view::npos || code[k] != ':' ||
               (k + 1 < code.size() && code[k + 1] == ':'))
                return false;
        }
        st().headers = nullptr;
        if(!acc && top_kind() == Kind::Switch && s.frames->case_ind < 0) {
            Frame f = *s.frames;
            f.case_ind = ind;
            s.frames = std::make_shared<const Frame>(std::move(f));
        }
        st().kw = Kw::None;
        st().paren_seen = st().init_list = false;
        st().angle = st().tern = 0, st().rhs_col = -1;
        st().after_hdr = false;
        st().last = ':';
        st().line = line;
        st().ind = ind;
        st().pending = acc ? Pending::None : Pending::Label;
        i = k + 1;
        return true;
    }

    void else_word() {
        HeaderPtr h = st().headers;
        HeaderPtr m;
        for(HeaderPtr p = h; p; p = p->parent)
            if(p->kind == HKind::If) {
                m = p;
                break;
            }
        int hind = ind;
        if(m) {
            hind = m->ind;
            h = m->parent;
            while(h && h->kind == HKind::Else) h = h->parent;
        }
        st().headers = push_header(h, hind, HKind::Else);
        st().pending = Pending::Header;
        st().kw = Kw::None;
        st().paren_seen = st().init_list = false;
        st().angle = st().tern = 0, st().rhs_col = -1;
        st().after_hdr = false;
        st().last = 'w';
    }

    void word(std::string_view w, size_t end) {
        const bool start = at_start();
        if(start && try_label(w, end)) return;
        if(start && w == "else") return else_word();
        if(start && w == "while" && st().headers &&
           st().headers->kind == HKind::Do) {
            const int dind = st().headers->ind;
            st().headers = st().headers->parent;
            st().pending = Pending::Cont;
            st().kw = Kw::Other;
            st().paren_seen = st().init_list = false;
            st().angle = st().tern = 0, st().rhs_col = -1;
            st().after_hdr = false;
            st().line = line;
            st().ind = dind;
            st().last = 'w';
            return;
        }
        const bool new_stmt = start;
        begin_stmt();
        Stmt& t = st();
        t.after_hdr = false;
        t.last = w == "return" ? '=' : w == "try" ? 't' : 'w';
        if(w == "return" && new_stmt) t.rhs_col = align_after(end - 1);
        if(new_stmt) {
            if(w == "switch") t.kw = Kw::Switch;
            else if(w == "namespace") t.kw = Kw::Scope;
            else if(w == "extern") {
                const size_t n = raw.find_first_not_of(" \t", end);
                t.kw = (n != std::string_view::npos && raw[n] == '"')
                           ? Kw::Scope
                           : Kw::Other;
            } else if(w == "class" || w == "struct" || w == "union")
                t.kw = Kw::Class;
            else if(w == "enum") t.kw = Kw::Enum;
            else if(w == "template") t.kw = Kw::Template;
            else t.kw = Kw::Other;
            if(w == "if" || w == "for" || w == "while") {
                t.headers = push_header(
                    t.headers, t.ind, w == "if" ? HKind::If : HKind::Other);
                t.after_hdr = true;
            } else if(w == "do") {
                t.headers = push_header(t.headers, t.ind, HKind::Do);
                t.pending = Pending::Header;
            }
        } else if((t.kw == Kw::Other || t.kw == Kw::Template) &&
                  !t.paren_seen && t.angle == 0) {
            if(w == "class" || w == "struct" || w == "union") t.kw = Kw::Class;
            else if(w == "enum") t.kw = Kw::Enum;
            else if(w == "namespace") t.kw = Kw::Scope;
        }
    }

    void run() {
        while(i < code.size()) {
            const char c = code[i];
            if(c == ' ' || c == '\t') {
                ++i;
                continue;
            }
            if(word_char(static_cast<unsigned char>(c))) {
                size_t e = i;
                while(e < code.size() &&
                      word_char(static_cast<unsigned char>(code[e])))
                    ++e;
                const std::string_view w = code.substr(i, e - i);
                i = e;
                word(w, e);
                continue;
            }
            const size_t at = i++;
            switch(c) {
                case ';':
                    if(in_group()) break;
                    st().pending = Pending::None;
                    st().kw = Kw::None;
                    st().paren_seen = st().init_list = false;
                    st().angle = st().tern = 0, st().rhs_col = -1;
                    st().after_hdr = false;
                    st().last = ';';
                    break;
                case ',':
                    st().after_hdr = false;
                    st().last = ',';
                    if(top_kind() == Kind::Init &&
                       st().pending != Pending::None) {
                        st().pending = Pending::None;
                        st().kw = Kw::None;
                        st().paren_seen = st().init_list = false;
                        st().angle = st().tern = 0, st().rhs_col = -1;
                    }
                    break;
                case '}': close('{'); break;
                case ')': close('('); break;
                case ']': close('['); break;
                case '{': {
                    const bool hb = st().pending == Pending::Header;
                    const int hind = hb && st().headers ? st().headers->ind : 0;
                    // `f({.a = 1,`: a list straight after a `(` / `[` is an
                    // init list, not a code block (begin_stmt() below wipes
                    // the evidence, so look now)
                    const bool first_arg =
                        !hb && in_group() && st().pending == Pending::None &&
                        st().last == 0;
                    begin_stmt();
                    const Kind k = brace_kind(hb, first_arg);
                    push(k,
                         '{',
                         align_after(at),
                         false,
                         hb && st().headers ? hind : st().ind);
                    break;
                }
                case '(':
                case '[': {
                    begin_stmt();
                    const bool hp = st().after_hdr;
                    st().after_hdr = false;
                    push(c == '(' ? Kind::Paren : Kind::Bracket,
                         c,
                         align_after(at),
                         hp,
                         st().ind);
                    break;
                }
                default:
                    begin_stmt();
                    if(c == '.' && st().last == 0) st().rhs_col = -2;
                    st().after_hdr = false;
                    st().last = c;
                    if(st().kw == Kw::Template) {
                        if(c == '<') ++st().angle;
                        else if(c == '>' && st().angle > 0) --st().angle;
                    }
                    if(c == '=' && st().rhs_col == -1) {
                        const char nx = i < code.size() ? code[i] : 0;
                        const char pv = at > 0 ? code[at - 1] : 0;
                        const bool shift = (pv == '<' || pv == '>') && at > 1 &&
                                           code[at - 2] == pv;
                        if(nx != '=' && pv != '=' && pv != '!' &&
                           (shift || (pv != '<' && pv != '>')))
                            st().rhs_col = align_after(at);
                    }
                    if(c == '?') {
                        const size_t f0 = code.find_first_not_of(" \t");
                        const bool lead =
                            f0 != std::string_view::npos && code[f0] == ':' &&
                            !(f0 + 1 < code.size() && code[f0 + 1] == ':');
                        ++st().tern;
                        st().tern_col =
                            lead ? cx_to_rx(raw, static_cast<int>(at)) : -1;
                    }
                    if(c == ':') {
                        if(i < code.size() && code[i] == ':') {
                            ++i;
                            st().last = 'w';
                        } else if(st().tern > 0) {
                            --st().tern;  // else-half of a ternary
                            st().tern_col = -1;
                        } else if(st().paren_seen) {
                            if(!st().init_list) {
                                st().init_ind = ind;
                                st().init_col = align_after(at);
                            }
                            st().init_list = true;
                        }
                    }
                    break;
            }
        }
    }
};

}  // namespace

void advance(LineState& s, std::string_view raw, std::string_view code, int ind,
             uint32_t line) {
    const size_t f = raw.find_first_not_of(" \t");
    if(s.pp_cont || (f != std::string_view::npos && raw[f] == '#')) {
        const size_t e = raw.find_last_not_of(" \t");
        s.pp_cont = e != std::string_view::npos && raw[e] == '\\';
        return;
    }
    const size_t first = code.find_first_not_of(" \t");
    if(first == std::string_view::npos) return;
    // the first element of an init list that starts a line sets the indent
    // the following ones copy
    if(const Frame* f = s.frames.get();
       f && f->kind == Kind::Init && f->line != line && f->sib_ind < 0 &&
       s.stmt.pending == Pending::None &&
       !(code[first] == '}' || code[first] == ')' || code[first] == ']')) {
        Frame nf = *f;
        nf.sib_ind = ind;
        s.frames = std::make_shared<const Frame>(std::move(nf));
    }
    Walker w{s, raw, code, ind, line};
    w.run();
    Stmt& t = s.stmt;
    if(t.kw == Kw::Template && t.angle == 0 && t.last == '>') {
        t.pending = Pending::None;
        t.kw = Kw::None;
    }
}

}  // namespace indent
