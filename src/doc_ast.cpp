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

// :Doc cursor detection from the syntax tree; see doc_ast.hpp.
#include "doc_ast.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace doc {
namespace {

using namespace std::string_view_literals;

// A node that joins two names: `a::b` (scope) or `a.b` / `a->b` (member).
struct Dotted {
    std::string_view kind, left, right;
    bool member;
};
struct Row {
    std::string_view lang;
    std::vector<Dotted> dotted;
};

const std::vector<Row>& rows() {
    static const std::vector<Row> t = {
        {"cpp",
         {{"qualified_identifier", "scope", "name", false},
          {"field_expression", "argument", "field", true}}},
        {"c", {{"field_expression", "argument", "field", true}}},
        {"lua",
         {{"dot_index_expression", "table", "field", true},
          {"method_index_expression", "table", "method", true}}},
        {"rust",
         {{"scoped_identifier", "path", "name", false},
          {"scoped_type_identifier", "path", "name", false},
          {"field_expression", "value", "field", true}}},
        {"python", {{"attribute", "object", "attribute", true}}},
        {"java", {{"field_access", "object", "field", true},
                  {"method_invocation", "object", "name", true}}},
        {"go", {{"selector_expression", "operand", "field", true}}},
    };
    return t;
}

constexpr uint32_t kEnd = std::numeric_limits<uint32_t>::max();

struct Ctx {
    const Row* row;
    std::string_view lang;
    const TextFn* text;
    bool cpp() const { return lang == "cpp" || lang == "c++"; }
    std::string txt(TSNode n) const {
        return (*text)(ts_node_start_byte(n), ts_node_end_byte(n));
    }
};

std::string_view kind(TSNode n) { return ts_node_type(n); }
bool nul(TSNode n) { return ts_node_is_null(n); }
TSNode field(TSNode n, std::string_view f) {
    return ts_node_child_by_field_name(n, f.data(), static_cast<uint32_t>(f.size()));
}
const Dotted* dotted_for(const Ctx& cx, TSNode n) {
    const std::string_view k = kind(n);
    for(const Dotted& d : cx.row->dotted)
        if(d.kind == k) return &d;
    return nullptr;
}
bool is_alpha_kw(std::string_view s) {
    if(s.size() < 2) return false;
    for(const char c : s)
        if(!(c >= 'a' && c <= 'z') && c != '_') return false;
    return true;
}

// ---- name chains ------------------------------------------------------------

struct Chain {
    std::vector<std::string> segs;  // up to and including the cursor's segment
    bool member = false;  // the last hop was a member access, not a scope
    TSNode recv{};        // that hop's left side
    bool recv_ok = false;  // ... and it is a plain name chain
    bool stopped = false;  // reached a segment after the cursor
};

// Flattens a chain of names, left to right, dropping template arguments.
// Stops before the first segment that starts after `byte`. False when `n` is
// not a name chain (a call, a subscript, `this` ...).
bool walk(const Ctx& cx, TSNode n, uint32_t byte, Chain& out) {
    if(nul(n)) return true;
    if(const Dotted* d = dotted_for(cx, n)) {
        const TSNode l = field(n, d->left), r = field(n, d->right);
        bool ok = nul(l) ? true : walk(cx, l, byte, out);
        if(out.stopped) return ok;
        if(!ok) out.segs.clear();  // opaque receiver: restart at this hop
        if(d->member && !nul(r) && ts_node_start_byte(r) <= byte) {
            out.member = true;
            out.recv = l;
            out.recv_ok = ok && !nul(l);
        }
        walk(cx, r, byte, out);
        return ok;
    }
    const std::string_view k = kind(n);
    if(k == "template_type") return walk(cx, field(n, "name"), byte, out);
    if(k == "this" || k == "self" || k == "super") return false;
    if(!ts_node_is_named(n) || ts_node_child_count(n) != 0) return false;
    if(ts_node_start_byte(n) > byte) {
        out.stopped = true;
        return true;
    }
    out.segs.push_back(cx.txt(n));
    return true;
}

std::string join(const std::vector<std::string>& v, size_t from = 0) {
    std::string o;
    for(size_t i = from; i < v.size(); ++i) o += (o.empty() ? "" : ".") + v[i];
    return o;
}

// Whole-node name: `std::vector<int>` -> "std.vector".
std::optional<std::string> full_name(const Ctx& cx, TSNode n) {
    Chain c;
    if(!walk(cx, n, kEnd, c) || c.segs.empty()) return std::nullopt;
    return join(c.segs);
}

// ---- using declarations -----------------------------------------------------

struct Usings {
    std::vector<std::string> scopes;   // using namespace X;
    std::vector<std::string> aliases;  // using X::y;  (full name, X.y)
};

Usings usings_at(const Ctx& cx, TSNode from) {
    Usings u;
    for(TSNode a = ts_node_parent(from); !nul(a); a = ts_node_parent(a)) {
        const uint32_t n = ts_node_child_count(a);
        for(uint32_t i = 0; i < n; ++i) {
            const TSNode c = ts_node_child(a, i);
            if(kind(c) != "using_declaration") continue;
            bool ns = false;
            std::optional<std::string> nm;
            for(uint32_t j = 0; j < ts_node_child_count(c); ++j) {
                const TSNode x = ts_node_child(c, j);
                if(!ts_node_is_named(x)) {
                    ns = ns || kind(x) == "namespace";
                } else if(kind(x) == "identifier" ||
                          kind(x) == "qualified_identifier") {
                    nm = full_name(cx, x);
                }
            }
            if(!nm) continue;
            (ns ? u.scopes : u.aliases).push_back(*nm);
        }
    }
    return u;
}

// std typedefs that have no page of their own: the page is the template's.
std::string_view cpp_alias(std::string_view n) {
    static const std::pair<std::string_view, std::string_view> t[] = {
        {"string", "basic_string"},
        {"wstring", "basic_string"},
        {"u8string", "basic_string"},
        {"u16string", "basic_string"},
        {"u32string", "basic_string"},
        {"string_view", "basic_string_view"},
        {"wstring_view", "basic_string_view"},
        {"u8string_view", "basic_string_view"},
        {"u16string_view", "basic_string_view"},
        {"u32string_view", "basic_string_view"},
        {"istream", "basic_istream"},
        {"ostream", "basic_ostream"},
        {"iostream", "basic_iostream"},
        {"ifstream", "basic_ifstream"},
        {"ofstream", "basic_ofstream"},
        {"fstream", "basic_fstream"},
        {"istringstream", "basic_istringstream"},
        {"ostringstream", "basic_ostringstream"},
        {"stringstream", "basic_stringstream"},
        {"streambuf", "basic_streambuf"},
        {"ios", "basic_ios"},
        {"regex", "basic_regex"},
        {"smatch", "match_results"},
        {"cmatch", "match_results"},
    };
    for(const auto& [k, v] : t)
        if(k == n) return v;
    return {};
}

// `name` as written, then every spelling the file's usings allow, then (C++)
// the std:: guess. First entry is always the name itself.
std::vector<std::string> variants(const Ctx& cx, TSNode at, const std::string& name) {
    std::vector<std::string> out{name};
    const auto add = [&](std::string s) {
        if(std::ranges::find(out, s) == out.end()) out.push_back(std::move(s));
    };
    if(!cx.cpp()) return out;
    const std::string head = name.substr(0, name.find('.'));
    const Usings u = usings_at(cx, at);
    for(const std::string& a : u.aliases) {
        const size_t p = a.rfind('.');
        if((p == std::string::npos ? a : a.substr(p + 1)) == head)
            add(a + name.substr(head.size()));
    }
    for(const std::string& s : u.scopes)
        if(name != s && !name.starts_with(s + ".")) add(s + "." + name);
    if(head != "std") add("std." + name);
    // typedefs DevDocs only documents under the real template:
    // std::string_view -> std::basic_string_view
    for(const std::string& v : std::vector<std::string>(out)) {
        const size_t p = v.rfind('.');
        const std::string last = p == std::string::npos ? v : v.substr(p + 1);
        if(const std::string_view a = cpp_alias(last); !a.empty())
            add((p == std::string::npos ? "" : v.substr(0, p + 1)) + std::string(a));
    }
    return out;
}

// ---- local declarations -----------------------------------------------------

struct Decl {
    TSNode type{}, value{};
};

// The identifier a declarator introduces, plus the initialiser when it has
// one. Skips function declarators (those are not variables).
bool declarator_matches(const Ctx& cx, TSNode d, const std::string& name,
                        TSNode& value) {
    for(int guard = 0; guard < 8 && !nul(d); ++guard) {
        const std::string_view k = kind(d);
        if(k == "identifier" || k == "field_identifier")
            return cx.txt(d) == name;
        if(k == "init_declarator") {
            value = field(d, "value");
            d = field(d, "declarator");
        } else if(k == "pointer_declarator" || k == "array_declarator" ||
                  k == "parenthesized_declarator") {
            d = field(d, "declarator");
            if(nul(d))  // parenthesized_declarator has no field
                return false;
        } else if(k == "reference_declarator") {
            d = ts_node_named_child(d, 0);
        } else {
            return false;
        }
    }
    return false;
}

bool decl_matches(const Ctx& cx, TSNode c, const std::string& name, Decl& out) {
    const std::string_view k = kind(c);
    if(k != "declaration" && k != "field_declaration" &&
       k != "parameter_declaration" && k != "optional_parameter_declaration" &&
       k != "for_range_loop")
        return false;
    const uint32_t n = ts_node_child_count(c);
    for(uint32_t i = 0; i < n; ++i) {
        const char* f = ts_node_field_name_for_child(c, i);
        if(!f || std::string_view(f) != "declarator") continue;
        TSNode value{};
        if(declarator_matches(cx, ts_node_child(c, i), name, value)) {
            out.type = field(c, "type");
            out.value = k == "for_range_loop" ? TSNode{} : value;
            return !nul(out.type);
        }
    }
    return false;
}

// The parameter_list of a function, method or lambda `a`, if it has one.
TSNode params_of(TSNode a) {
    TSNode p = field(a, "parameters");
    if(!nul(p)) return p;
    TSNode d = field(a, "declarator");
    for(int guard = 0; guard < 6 && !nul(d); ++guard) {
        p = field(d, "parameters");
        if(!nul(p)) return p;
        d = field(d, "declarator");
    }
    return TSNode{};
}

std::optional<std::string> local_type(const Ctx& cx, TSNode id, int depth = 0);

std::optional<std::string> type_name(const Ctx& cx, const Decl& d, int depth) {
    const std::string_view k = kind(d.type);
    if(k == "placeholder_type_specifier") {  // auto: ask the initialiser
        if(depth > 3 || nul(d.value)) return std::nullopt;
        const std::string_view vk = kind(d.value);
        if(vk == "identifier") return local_type(cx, d.value, depth + 1);
        if(vk == "call_expression") return full_name(cx, field(d.value, "function"));
        if(vk == "new_expression") return full_name(cx, field(d.value, "type"));
        return std::nullopt;
    }
    if(k == "struct_specifier" || k == "class_specifier" ||
       k == "enum_specifier" || k == "union_specifier")
        return full_name(cx, field(d.type, "name"));
    return full_name(cx, d.type);
}

// The declared type of the variable `id` names, from the declarations visible
// at that point. Nearest scope first; inside a block, the last one before use.
std::optional<std::string> local_type(const Ctx& cx, TSNode id, int depth) {
    const std::string name = cx.txt(id);
    const uint32_t use = ts_node_start_byte(id);
    for(TSNode a = ts_node_parent(id); !nul(a); a = ts_node_parent(a)) {
        Decl d, best;
        bool have = false;
        const TSNode ps = params_of(a);
        if(!nul(ps))
            for(uint32_t i = 0; i < ts_node_child_count(ps); ++i)
                if(decl_matches(cx, ts_node_child(ps, i), name, d))
                    best = d, have = true;
        if(kind(a) == "for_range_loop" && decl_matches(cx, a, name, d))
            best = d, have = true;
        const bool block = kind(a) == "compound_statement" ||
                           kind(a) == "translation_unit" ||
                           kind(a) == "declaration_list";
        for(uint32_t i = 0; i < ts_node_child_count(a); ++i) {
            const TSNode c = ts_node_child(a, i);
            if(block && ts_node_start_byte(c) > use) break;
            if(decl_matches(cx, c, name, d)) best = d, have = true;
        }
        if(have) return type_name(cx, best, depth);
    }
    return std::nullopt;
}

// ---- the query --------------------------------------------------------------

void push(Query& q, std::string name, bool raw = false) {
    for(const Cand& c : q.cands)
        if(c.name == name && c.raw == raw) return;
    q.cands.push_back({std::move(name), raw});
}

std::string strip_header(std::string s) {
    if(s.size() >= 2 && ((s.front() == '<' && s.back() == '>') ||
                         (s.front() == '"' && s.back() == '"')))
        return s.substr(1, s.size() - 2);
    return s;
}

}  // namespace

std::optional<Query> query_at(const TSTree* tree,
                              std::string_view lang,
                              const TextFn& text,
                              size_t byte) {
    if(!tree) return std::nullopt;
    const Row* row = nullptr;
    for(const Row& r : rows())
        if(r.lang == lang || (lang == "c++" && r.lang == "cpp")) row = &r;
    if(!row) return std::nullopt;
    const Ctx cx{row, lang, &text};

    const uint32_t b = static_cast<uint32_t>(byte);
    const TSNode leaf = ts_node_descendant_for_byte_range(
        ts_tree_root_node(tree), b, b + 1);
    if(nul(leaf) || ts_node_child_count(leaf) != 0) return std::nullopt;
    const std::string_view lk = kind(leaf);
    Query q;

    // #include <vector>
    if(const TSNode p = ts_node_parent(leaf);
       !nul(p) && kind(p) == "preproc_include") {
        const std::string raw = cx.txt(leaf);
        const std::string stem = strip_header(raw);
        if(stem.empty() || stem == "include") return std::nullopt;
        push(q, raw, true);
        push(q, stem, true);
        if(cx.cpp() && stem.find('.') == std::string::npos)
            push(q, "std." + stem);
        q.seed = stem;
        return q;
    }

    if(lk.find("comment") != std::string_view::npos ||
       lk.find("string") != std::string_view::npos)
        return std::nullopt;

    // keywords: nodes spelled like a word (for, auto, constexpr). Anonymous
    // ones always; named ones only when the node kind is the token itself.
    if(const std::string s = cx.txt(leaf);
       !ts_node_is_named(leaf) || (lk == s && is_alpha_kw(s))) {
        if(!is_alpha_kw(s)) return std::nullopt;
        push(q, s);
        q.seed = s;
        return q;
    }

    // names: climb to the outermost scope/member chain the leaf belongs to
    TSNode top = leaf;
    for(TSNode p = ts_node_parent(top); !nul(p); p = ts_node_parent(top)) {
        if(!dotted_for(cx, p) && kind(p) != "template_type") break;
        top = p;
    }
    Chain ch;
    walk(cx, top, b, ch);
    if(ch.segs.empty()) return std::nullopt;
    const std::string full = join(ch.segs);
    q.seed = ch.segs.back();

    if(ch.member) {
        q.member = ch.segs.back();
        if(ch.recv_ok && kind(ch.recv) == "identifier" && cx.cpp()) {
            if(const auto t = local_type(cx, ch.recv))
                for(const std::string& v : variants(cx, ch.recv, *t))
                    push(q, v + "." + q.member);
        }
        if(ch.recv_ok) push(q, full);  // module style: string.format, fmt.Println
        return q;
    }

    if(ch.segs.size() == 1 && kind(leaf) == "identifier" && cx.cpp()) {
        if(const auto t = local_type(cx, leaf)) {  // a variable: its type's page
            for(const std::string& v : variants(cx, leaf, *t)) push(q, v);
            return q;
        }
    }
    for(const std::string& v : variants(cx, leaf, full)) push(q, v);
    // std::X with no page under that name: the bare name is often the header
    // page (<string_view>); last resort, after every exact spelling
    if(cx.cpp() && ch.segs.size() == 2 && ch.segs[0] == "std") push(q, ch.segs[1]);
    return q;
}

}  // namespace doc
