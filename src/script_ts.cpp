// winter.ts: the buffer's syntax tree and tree-sitter queries for plugins.
//
// A node is a userdata that remembers the buffer it came from and the tree
// generation it was taken under. Editing the buffer changes the tree, so every
// method first checks the generation and raises "stale node" instead of
// touching memory that moved. Fetch nodes again after an edit.
//
// Lines and columns are 1-based like the rest of winter.*; a range's end column
// is exclusive. Nodes of the main tree only: injected languages are not exposed.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <lua.hpp>
#include <memory>
#include <string>
#include <vector>

#include "script_internal.hpp"
#include "tsapi.hpp"
#include "tsq.hpp"

namespace {

TsProvider g_p;

constexpr const char* kNodeMT = "winter.ts.node";
constexpr const char* kQueryMT = "winter.ts.query";

struct LNode {
    TSNode node;
    uint64_t buf;
    uint64_t gen;
};

struct LQuery {
    std::unique_ptr<TsQuery> q;
    const TSLanguage* lang;
    std::string lang_name;
};

bool snap(uint64_t buf, TsSnapshot& s) {
    return g_p.snapshot && g_p.snapshot(buf, s);
}

// Is the node still valid? (Own scope for the snapshot: callers raise AFTER it
// is gone, a longjmp must not skip a destructor.)
bool fresh(const LNode& n) {
    TsSnapshot s;
    return snap(n.buf, s) && s.tree && s.gen == n.gen;
}

int stale(lua_State* L) {
    return luaL_error(L,
                      "stale node: the buffer changed since it was fetched "
                      "(fetch the node again)");
}

LNode* check_node(lua_State* L, int i) {
    return static_cast<LNode*>(luaL_checkudata(L, i, kNodeMT));
}

void push_node(lua_State* L, TSNode nd, const LNode& from) {
    auto* u = static_cast<LNode*>(lua_newuserdatauv(L, sizeof(LNode), 0));
    *u = LNode{nd, from.buf, from.gen};
    luaL_setmetatable(L, kNodeMT);
}

// nil for a null node (no parent, no sibling, ...)
void push_node_or_nil(lua_State* L, TSNode nd, const LNode& from) {
    if(ts_node_is_null(nd)) lua_pushnil(L);
    else push_node(L, nd, from);
}

void push_range(lua_State* L, TSNode nd) {
    const TSPoint a = ts_node_start_point(nd), b = ts_node_end_point(nd);
    lua_pushinteger(L, static_cast<lua_Integer>(a.row) + 1);
    lua_pushinteger(L, static_cast<lua_Integer>(a.column) + 1);
    lua_pushinteger(L, static_cast<lua_Integer>(b.row) + 1);
    lua_pushinteger(L, static_cast<lua_Integer>(b.column) + 1);
}

TSPoint point_arg(lua_State* L, int line, int col) {
    const lua_Integer l = luaL_checkinteger(L, line), c = luaL_checkinteger(L, col);
    return TSPoint{static_cast<uint32_t>(std::max<lua_Integer>(l - 1, 0)),
                   static_cast<uint32_t>(std::max<lua_Integer>(c - 1, 0))};
}

// ---- node methods -------------------------------------------------------------------

#define NODE_OR_STALE(n)                                   \
    LNode* n = check_node(L, 1);                           \
    if(!fresh(*n)) return stale(L);

int n_type(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushstring(L, ts_node_type(n->node));
    return 1;
}
int n_is_named(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushboolean(L, ts_node_is_named(n->node));
    return 1;
}
int n_is_missing(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushboolean(L, ts_node_is_missing(n->node));
    return 1;
}
int n_is_extra(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushboolean(L, ts_node_is_extra(n->node));
    return 1;
}
int n_has_error(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushboolean(L, ts_node_has_error(n->node));
    return 1;
}
int n_is_error(lua_State* L) {
    NODE_OR_STALE(n)
    // the ERROR node: the symbol tree-sitter reserves as (TSSymbol)-1
    lua_pushboolean(L, ts_node_symbol(n->node) == static_cast<TSSymbol>(-1));
    return 1;
}

// node:range() -> line1, col1, line2, col2   (end column exclusive)
int n_range(lua_State* L) {
    NODE_OR_STALE(n)
    push_range(L, n->node);
    return 4;
}

// node:byte_range() -> start, end   (0-based byte offsets, end exclusive)
int n_byte_range(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushinteger(L, ts_node_start_byte(n->node));
    lua_pushinteger(L, ts_node_end_byte(n->node));
    return 2;
}

int n_text(lua_State* L) {
    LNode* n = check_node(L, 1);
    bool ok = false;
    std::string t;
    {
        TsSnapshot s;
        if(snap(n->buf, s) && s.tree && s.gen == n->gen) {
            ok = true;
            t = s.text(ts_node_start_byte(n->node), ts_node_end_byte(n->node));
        }
    }
    if(!ok) return stale(L);
    lua_pushlstring(L, t.data(), t.size());
    return 1;
}

int n_parent(lua_State* L) {
    NODE_OR_STALE(n)
    push_node_or_nil(L, ts_node_parent(n->node), *n);
    return 1;
}
int n_next_sibling(lua_State* L) {
    NODE_OR_STALE(n)
    push_node_or_nil(L, ts_node_next_sibling(n->node), *n);
    return 1;
}
int n_prev_sibling(lua_State* L) {
    NODE_OR_STALE(n)
    push_node_or_nil(L, ts_node_prev_sibling(n->node), *n);
    return 1;
}
int n_next_named_sibling(lua_State* L) {
    NODE_OR_STALE(n)
    push_node_or_nil(L, ts_node_next_named_sibling(n->node), *n);
    return 1;
}
int n_prev_named_sibling(lua_State* L) {
    NODE_OR_STALE(n)
    push_node_or_nil(L, ts_node_prev_named_sibling(n->node), *n);
    return 1;
}

int n_child_count(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushinteger(L, ts_node_child_count(n->node));
    return 1;
}
int n_named_child_count(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushinteger(L, ts_node_named_child_count(n->node));
    return 1;
}

// node:child(i) / node:named_child(i): 1-based; nil past the end
int n_child(lua_State* L) {
    NODE_OR_STALE(n)
    const lua_Integer i = luaL_checkinteger(L, 2);
    if(i < 1 || i > ts_node_child_count(n->node)) lua_pushnil(L);
    else push_node(L, ts_node_child(n->node, static_cast<uint32_t>(i - 1)), *n);
    return 1;
}
int n_named_child(lua_State* L) {
    NODE_OR_STALE(n)
    const lua_Integer i = luaL_checkinteger(L, 2);
    if(i < 1 || i > ts_node_named_child_count(n->node)) lua_pushnil(L);
    else push_node(L, ts_node_named_child(n->node, static_cast<uint32_t>(i - 1)), *n);
    return 1;
}

int children_of(lua_State* L, bool named) {
    NODE_OR_STALE(n)
    const uint32_t count =
        named ? ts_node_named_child_count(n->node) : ts_node_child_count(n->node);
    lua_createtable(L, static_cast<int>(count), 0);
    for(uint32_t i = 0; i < count; ++i) {
        push_node(L,
                  named ? ts_node_named_child(n->node, i) : ts_node_child(n->node, i),
                  *n);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
    }
    return 1;
}
int n_children(lua_State* L) { return children_of(L, false); }
int n_named_children(lua_State* L) { return children_of(L, true); }

// node:field("name") -> list of the children that have that field name
int n_field(lua_State* L) {
    NODE_OR_STALE(n)
    const char* want = luaL_checkstring(L, 2);
    lua_newtable(L);
    lua_Integer k = 1;
    const uint32_t count = ts_node_child_count(n->node);
    for(uint32_t i = 0; i < count; ++i) {
        const char* f = ts_node_field_name_for_child(n->node, i);
        if(!f || std::string_view(f) != want) continue;
        push_node(L, ts_node_child(n->node, i), *n);
        lua_rawseti(L, -2, k++);
    }
    return 1;
}

// node:descendant(line1, col1 [, line2, col2]): the smallest node covering the
// range (a point when the end is left out). named_descendant skips anonymous ones.
int descendant_of(lua_State* L, bool named) {
    NODE_OR_STALE(n)
    const TSPoint a = point_arg(L, 2, 3);
    const TSPoint b = lua_gettop(L) >= 5 ? point_arg(L, 4, 5) : a;
    const TSNode d = named ? ts_node_named_descendant_for_point_range(n->node, a, b)
                           : ts_node_descendant_for_point_range(n->node, a, b);
    push_node_or_nil(L, d, *n);
    return 1;
}
int n_descendant(lua_State* L) { return descendant_of(L, false); }
int n_named_descendant(lua_State* L) { return descendant_of(L, true); }

// node:id(): a number that identifies the node within its tree (a table key)
int n_id(lua_State* L) {
    NODE_OR_STALE(n)
    lua_pushinteger(L, static_cast<lua_Integer>(reinterpret_cast<uintptr_t>(n->node.id)));
    return 1;
}

int n_sexpr(lua_State* L) {
    NODE_OR_STALE(n)
    char* s = ts_node_string(n->node);
    lua_pushstring(L, s);
    std::free(s);
    return 1;
}

int n_buf(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(check_node(L, 1)->buf));
    return 1;
}

int n_lang(lua_State* L) {
    LNode* n = check_node(L, 1);
    std::string lang;
    bool ok = false;
    {
        TsSnapshot s;
        if(snap(n->buf, s) && s.tree && s.gen == n->gen) {
            ok = true;
            lang = s.lang;
        }
    }
    if(!ok) return stale(L);
    lua_pushlstring(L, lang.data(), lang.size());
    return 1;
}

int n_equals(lua_State* L) {
    const LNode* a = check_node(L, 1);
    const LNode* b = check_node(L, 2);
    lua_pushboolean(L, a->buf == b->buf && a->gen == b->gen && ts_node_eq(a->node, b->node));
    return 1;
}

int n_tostring(lua_State* L) {
    LNode* n = check_node(L, 1);
    const char* type = fresh(*n) ? ts_node_type(n->node) : "stale";
    lua_pushfstring(L, "node(%s)", type);
    return 1;
}

const luaL_Reg kNodeMethods[] = {{"type", n_type},
                                 {"is_named", n_is_named},
                                 {"is_missing", n_is_missing},
                                 {"is_extra", n_is_extra},
                                 {"has_error", n_has_error},
                                 {"is_error", n_is_error},
                                 {"range", n_range},
                                 {"byte_range", n_byte_range},
                                 {"text", n_text},
                                 {"parent", n_parent},
                                 {"next_sibling", n_next_sibling},
                                 {"prev_sibling", n_prev_sibling},
                                 {"next_named_sibling", n_next_named_sibling},
                                 {"prev_named_sibling", n_prev_named_sibling},
                                 {"child_count", n_child_count},
                                 {"named_child_count", n_named_child_count},
                                 {"child", n_child},
                                 {"named_child", n_named_child},
                                 {"children", n_children},
                                 {"named_children", n_named_children},
                                 {"field", n_field},
                                 {"descendant", n_descendant},
                                 {"named_descendant", n_named_descendant},
                                 {"id", n_id},
                                 {"sexpr", n_sexpr},
                                 {"buf", n_buf},
                                 {"lang", n_lang},
                                 {"equals", n_equals},
                                 {nullptr, nullptr}};

const luaL_Reg kNodeMeta[] = {
    {"__eq", n_equals}, {"__tostring", n_tostring}, {nullptr, nullptr}};

// ---- queries ----------------------------------------------------------------------------

LQuery* check_query(lua_State* L, int i) {
    return static_cast<LQuery*>(luaL_checkudata(L, i, kQueryMT));
}

int q_gc(lua_State* L) {
    static_cast<LQuery*>(luaL_checkudata(L, 1, kQueryMT))->~LQuery();
    return 0;
}

// opts: first / last (1-based inclusive lines), limit
struct RunOpts {
    TsQuery::Lines lines;
    size_t limit = static_cast<size_t>(1) << 30;
};
RunOpts read_run_opts(lua_State* L, int t) {
    RunOpts o;
    if(!lua_istable(L, t)) return o;
    lua_getfield(L, t, "first");
    if(lua_isnumber(L, -1)) o.lines.first = static_cast<int>(lua_tointeger(L, -1)) - 1;
    lua_pop(L, 1);
    lua_getfield(L, t, "last");
    if(lua_isnumber(L, -1)) o.lines.last = static_cast<int>(lua_tointeger(L, -1)) - 1;
    lua_pop(L, 1);
    lua_getfield(L, t, "limit");
    if(lua_isnumber(L, -1) && lua_tointeger(L, -1) > 0)
        o.limit = static_cast<size_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return o;
}

// What a run needs, gathered with every snapshot-owning object gone again.
enum class RunStatus { Ok, Stale, WrongLang };

template <class F>
RunStatus run_on(const LQuery& q, const LNode& root, F&& fn) {
    TsSnapshot s;
    if(!snap(root.buf, s) || !s.tree || s.gen != root.gen) return RunStatus::Stale;
    if(ts_tree_language(root.node.tree) != q.lang) return RunStatus::WrongLang;
    fn(s.text);
    return RunStatus::Ok;
}

int run_failed(lua_State* L, RunStatus st, const char* lang) {
    if(st == RunStatus::Stale) return stale(L);
    return luaL_error(L, "this query was compiled for '%s', not this node's language",
                      lang);
}

void push_capture(lua_State* L, const LQuery& q, const TsQuery::Cap& c,
                  const LNode& from) {
    lua_createtable(L, 0, 2);
    const std::string name = q.q->capture_name(c.index);
    lua_pushlstring(L, name.data(), name.size());
    lua_setfield(L, -2, "name");
    push_node(L, c.node, from);
    lua_setfield(L, -2, "node");
}

// query:captures(node [, {first=, last=, limit=}]) ->
//   { {name=, node=, pattern=}, ... }  in document order
int q_captures(lua_State* L) {
    LQuery* q = check_query(L, 1);
    LNode* root = check_node(L, 2);
    const RunOpts o = read_run_opts(L, 3);
    std::vector<TsQuery::Hit> hits;
    const RunStatus st = run_on(*q, *root, [&](const TsText& text) {
        hits = q->q->captures(root->node, o.lines, o.limit, text);
    });
    if(st != RunStatus::Ok) return run_failed(L, st, q->lang_name.c_str());
    lua_createtable(L, static_cast<int>(hits.size()), 0);
    lua_Integer i = 1;
    for(const TsQuery::Hit& h : hits) {
        push_capture(L, *q, h.cap, *root);
        lua_pushinteger(L, static_cast<lua_Integer>(h.pattern) + 1);
        lua_setfield(L, -2, "pattern");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// query:matches(node [, {first=, last=, limit=}]) ->
//   { {pattern=, captures={ {name=, node=}, ... }, named={ name = node }}, ... }
// `named` holds the first node of each capture name.
int q_matches(lua_State* L) {
    LQuery* q = check_query(L, 1);
    LNode* root = check_node(L, 2);
    const RunOpts o = read_run_opts(L, 3);
    std::vector<TsQuery::Match> ms;
    const RunStatus st = run_on(*q, *root, [&](const TsText& text) {
        ms = q->q->matches(root->node, o.lines, o.limit, text);
    });
    if(st != RunStatus::Ok) return run_failed(L, st, q->lang_name.c_str());
    lua_createtable(L, static_cast<int>(ms.size()), 0);
    lua_Integer i = 1;
    for(const TsQuery::Match& m : ms) {
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, static_cast<lua_Integer>(m.pattern) + 1);
        lua_setfield(L, -2, "pattern");
        lua_createtable(L, static_cast<int>(m.caps.size()), 0);
        lua_createtable(L, 0, static_cast<int>(m.caps.size()));  // named
        lua_Integer k = 1;
        for(const TsQuery::Cap& c : m.caps) {
            push_capture(L, *q, c, *root);
            const std::string name = q->q->capture_name(c.index);
            lua_getfield(L, -2, name.c_str());
            const bool first = lua_isnil(L, -1);
            lua_pop(L, 1);
            if(first) {
                lua_getfield(L, -1, "node");
                lua_setfield(L, -3, name.c_str());
            }
            lua_rawseti(L, -3, k++);
        }
        lua_setfield(L, -3, "named");
        lua_setfield(L, -2, "captures");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int q_capture_names(lua_State* L) {
    const LQuery* q = check_query(L, 1);
    const uint32_t n = q->q->capture_count();
    lua_createtable(L, static_cast<int>(n), 0);
    for(uint32_t i = 0; i < n; ++i) {
        const std::string name = q->q->capture_name(i);
        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
    }
    return 1;
}

int q_pattern_count(lua_State* L) {
    lua_pushinteger(L, check_query(L, 1)->q->pattern_count());
    return 1;
}

int q_lang(lua_State* L) {
    const LQuery* q = check_query(L, 1);
    lua_pushlstring(L, q->lang_name.data(), q->lang_name.size());
    return 1;
}

const luaL_Reg kQueryMethods[] = {{"captures", q_captures},
                                  {"matches", q_matches},
                                  {"capture_names", q_capture_names},
                                  {"pattern_count", q_pattern_count},
                                  {"lang", q_lang},
                                  {nullptr, nullptr}};

// ---- module functions ---------------------------------------------------------------------

int fail(lua_State* L, const std::string& msg) {
    lua_pushnil(L);
    lua_pushlstring(L, msg.data(), msg.size());
    return 2;
}

// the root node of a buffer, or the reason there is none
bool root_of(uint64_t buf, LNode& out, std::string& err) {
    TsSnapshot s;
    if(!snap(buf, s)) {
        err = "no such buffer";
        return false;
    }
    if(!s.tree) {
        err = s.lang.empty() ? "this buffer has no language"
                             : "no syntax tree (no parser for '" + s.lang + "'?)";
        return false;
    }
    out = LNode{ts_tree_root_node(s.tree), s.buf, s.gen};
    return true;
}

// winter.ts.root([buf=0]) -> node | nil, err
int l_root(lua_State* L) {
    const uint64_t buf = static_cast<uint64_t>(luaL_optinteger(L, 1, 0));
    LNode r{};
    std::string err;
    if(!root_of(buf, r, err)) return fail(L, err);
    push_node(L, r.node, r);
    return 1;
}

// winter.ts.node_at(buf, line, col [, {named=false}]) -> node | nil, err
// The smallest node containing the position; named = true skips anonymous
// tokens like punctuation and keywords.
int l_node_at(lua_State* L) {
    const uint64_t buf = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    const TSPoint p = point_arg(L, 2, 3);
    bool named = false;
    if(lua_istable(L, 4)) {
        lua_getfield(L, 4, "named");
        named = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    LNode r{};
    std::string err;
    if(!root_of(buf, r, err)) return fail(L, err);
    const TSNode d = named ? ts_node_named_descendant_for_point_range(r.node, p, p)
                           : ts_node_descendant_for_point_range(r.node, p, p);
    push_node_or_nil(L, d, r);
    return 1;
}

// winter.ts.lang([buf=0]) -> the language of the buffer's tree | nil
int l_lang(lua_State* L) {
    const uint64_t buf = static_cast<uint64_t>(luaL_optinteger(L, 1, 0));
    std::string lang;
    bool have = false;
    {
        TsSnapshot s;
        if(snap(buf, s) && s.tree) {
            have = true;
            lang = s.lang;
        }
    }
    if(!have) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, lang.data(), lang.size());
    return 1;
}

// winter.ts.query(lang, source) -> query | nil, err
// Predicates: #eq? #not-eq? #match? #not-match? #any-of? #not-any-of? (regex
// is ECMAScript syntax); directives such as #set! are accepted and ignored.
int l_query(lua_State* L) {
    size_t n = 0, sn = 0;
    const char* lang = luaL_checklstring(L, 1, &n);
    const char* src = luaL_checklstring(L, 2, &sn);
    std::string err;
    std::unique_ptr<TsQuery> q;
    const TSLanguage* tl = nullptr;
    {
        const std::string name(lang, n);
        tl = g_p.language ? g_p.language(name) : nullptr;
        if(!tl) {
            err = "no parser for '" + name + "'";
        } else {
            q = TsQuery::compile(tl, std::string_view(src, sn), err);
        }
    }
    if(!q) return fail(L, err);
    void* mem = lua_newuserdatauv(L, sizeof(LQuery), 0);
    new(mem) LQuery{std::move(q), tl, std::string(lang, n)};
    luaL_setmetatable(L, kQueryMT);
    return 1;
}

const luaL_Reg kTs[] = {{"root", l_root},
                        {"node_at", l_node_at},
                        {"lang", l_lang},
                        {"query", l_query},
                        {nullptr, nullptr}};

void make_meta(lua_State* L, const char* name, const luaL_Reg* methods,
               const luaL_Reg* meta) {
    luaL_newmetatable(L, name);
    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, meta, 0);
    lua_pop(L, 1);
}

const luaL_Reg kQueryMeta[] = {{"__gc", q_gc}, {nullptr, nullptr}};

}  // namespace

void ts_set_provider(TsProvider p) { g_p = std::move(p); }

void script_open_ts() {
    lua_State* L = script_state();
    make_meta(L, kNodeMT, kNodeMethods, kNodeMeta);
    make_meta(L, kQueryMT, kQueryMethods, kQueryMeta);
    script_register("ts", kTs);
}
