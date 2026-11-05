#include <cctype>
#include <string_view>
#include "indent_model.hpp"

namespace indent {
namespace {

bool word_char(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

std::string_view first_word(std::string_view s) {
    size_t i = 0;
    while(i < s.size() && word_char(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(0, i);
}

const Frame* find_frame(const LineState& s, char open) {
    for(const Frame* f = s.frames.get(); f; f = f->parent.get())
        if(f->open == open) return f;
    return nullptr;
}

bool single_colon(std::string_view rest) {
    const size_t k = rest.find_first_not_of(" \t");
    return k != std::string_view::npos && rest[k] == ':' &&
           !(k + 1 < rest.size() && rest[k + 1] == ':');
}

// the line starts with something that cannot begin a new declaration
bool continues(std::string_view cur) {
    if(cur.empty()) return false;
    const unsigned char c = static_cast<unsigned char>(cur[0]);
    if(word_char(c) || c == '~' || c == '[') return false;
    return !cur.starts_with("::");
}

int inner(const Frame* f, const Cfg& c, const Peek& pk) {
    if(!f) return 0;
    const int own = f->owner_ind;
    switch(f->kind) {
        case Kind::Scope: {
            if(c.ns == 0) return own;
            if(c.ns == 1) return own + c.sw;
            int seen = f->first_ind;
            if(seen < 0 && pk.next_inner)
                if(const auto v = pk.next_inner()) seen = *v;
            return seen < 0 || seen > own ? own + c.sw : own;
        }
        case Kind::Switch: return (f->case_ind >= 0 ? f->case_ind : own) + c.sw;
        case Kind::Init: return f->align >= 0 ? f->align : own + c.sw;
        default: return own + c.sw;
    }
}

}  // namespace

std::optional<int> column(const LineState& s, std::string_view cur,
                          const Cfg& cfg, const Peek& pk) {
    const int sw = cfg.sw;
    const Frame* top = s.frames.get();
    const char c0 = cur.empty() ? '\0' : cur[0];
    const std::string_view w = first_word(cur);

    if(c0 == '}') {
        const Frame* f = find_frame(s, '{');
        return f ? std::optional<int>(f->owner_ind) : std::nullopt;
    }
    if(c0 == ')' || c0 == ']') {
        const Frame* f = find_frame(s, c0 == ')' ? '(' : '[');
        return f ? std::optional<int>(f->owner_ind) : std::nullopt;
    }
    if(top && (top->kind == Kind::Paren || top->kind == Kind::Bracket))
        return top->align >= 0 ? top->align : top->owner_ind + sw;

    if(top && top->kind == Kind::Switch) {
        const bool lab = w == "case" ||
                         (w == "default" && single_colon(cur.substr(w.size())));
        if(lab) {
            bool in = true;
            if(cfg.icase == 0) in = false;
            else if(cfg.icase == 2) {
                int seen = top->case_ind;
                if(seen < 0 && pk.next_label)
                    if(const auto v = pk.next_label()) seen = *v;
                in = seen < 0 || seen > top->owner_ind;
            }
            return top->owner_ind + (in ? sw : 0);
        }
    }
    if(top && top->kind == Kind::Class &&
       (w == "public" || w == "private" || w == "protected") &&
       single_colon(cur.substr(w.size())))
        return top->owner_ind;

    const Stmt& t = s.stmt;
    switch(t.pending) {
        case Pending::Label: return t.ind + sw;
        case Pending::Header: {
            const Header* h = t.headers.get();
            if(!h) return inner(top, cfg, pk);
            return c0 == '{' ? h->ind : h->ind + sw;
        }
        case Pending::None: {
            if(!t.headers) return inner(top, cfg, pk);
            const Header* outer = t.headers.get();
            while(outer->parent) outer = outer->parent.get();
            if(w == "else")
                for(const Header* h = t.headers.get(); h; h = h->parent.get())
                    if(h->kind == HKind::If) return h->ind;
            return outer->ind;
        }
        case Pending::Cont: break;
    }
    if(c0 == ':' && t.tern > 0 && t.tern_col >= 0 && cur.substr(0, 2) != "::")
        return t.tern_col;
    if(c0 == '{') return t.ind;
    if(t.init_list) {
        const int col = t.init_col >= 0 ? t.init_col : t.init_ind + 2;
        return c0 == ',' ? col - 2 : col;
    }
    const bool decl =
        !top || top->kind == Kind::Scope || top->kind == Kind::Class;
    if(decl && t.last == 'w' && !continues(cur)) return t.ind;
    if(t.rhs_col >= 0 && c0 != '.' && c0 != '?' && c0 != ':' &&
       !cur.starts_with("->"))
        return t.rhs_col;
    return t.ind + sw;
}

}  // namespace indent
