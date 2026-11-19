// winter.keymap, winter.action, winter.cmd, winter.ext, winter.ev
#include <fnmatch.h>
#include <algorithm>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "command.hpp"
#include "editor.hpp"
#include "errparse.hpp"
#include "events.hpp"
#include "extend.hpp"
#include "motion.hpp"
#include "registry.hpp"
#include "script_internal.hpp"
#include "term_panel.hpp"

namespace {

struct Modes {
    EditorMode m[8];
    int n = 0;
};

// "n", "nx", "nic" ... n i r v/x c s. False on an unknown letter.
bool parse_modes(std::string_view s, Modes& out) {
    for(const char c : s) {
        EditorMode m;
        switch(c) {
            case 'n': m = NORMAL; break;
            case 'i': m = INSERT; break;
            case 'r': m = REPLACE; break;
            case 'v':
            case 'x': m = VISUAL; break;
            case 'c': m = COMMAND; break;
            case 's': m = SEARCH; break;
            case 'f': m = FILES; break;
            case 't': m = TERMINAL; break;
            default: return false;
        }
        if(std::find(out.m, out.m + out.n, m) == out.m + out.n && out.n < 8)
            out.m[out.n++] = m;
    }
    return out.n > 0;
}

constexpr const char* kModesHelp =
    "modes: letters from n i r v x c s f t, a word like \"normal\", or a table "
    "of those";

// "normal" -> "n" ...; anything else is passed on as letters
std::string_view mode_word(std::string_view s) {
    static constexpr std::pair<std::string_view, std::string_view> kWords[] = {
        {"normal", "n"},
        {"insert", "i"},
        {"replace", "r"},
        {"visual", "v"},
        {"command", "c"},
        {"search", "s"},
        {"files", "f"},
        {"terminal", "t"},
    };
    for(const auto& [word, letters] : kWords)
        if(s == word) return letters;
    return s;
}

// Lua argument `idx` -> modes. Accepts "n", "nx", "normal", {"n", "x"},
// {"normal", "visual"}, {"nx", "i"}...
bool collect_modes(lua_State* L, int idx, Modes& out) {
    idx = lua_absindex(L, idx);
    if(lua_type(L, idx) == LUA_TSTRING) {
        size_t n;
        const char* s = lua_tolstring(L, idx, &n);
        return parse_modes(mode_word({s, n}), out);
    }
    if(!lua_istable(L, idx)) return false;
    const int len = static_cast<int>(lua_rawlen(L, idx));
    for(int i = 1; i <= len; ++i) {
        lua_rawgeti(L, idx, i);
        size_t n = 0;
        const char* s =
            lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &n) : nullptr;
        const bool ok = s && parse_modes(mode_word({s, n}), out);
        lua_pop(L, 1);
        if(!ok) return false;
    }
    return out.n > 0;
}

// a Lua function as an Action. It receives {count=, arg=}
Action make_action(LuaFn fn) {
    return [fn](Ctx& c) {
        lua_State* L = script_state();
        if(!L) return;
        fn->push();
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, c.count);
        lua_setfield(L, -2, "count");
        lua_pushlstring(L, c.arg.data(), c.arg.size());
        lua_setfield(L, -2, "arg");
        script_call(1);
    };
}

// Anonymous actions (a function passed straight to keymap.set) are counted per
// key they are mapped on; the action and its Lua function go when the last
// key is unmapped or remapped.
int g_next_anon = 1;
std::map<std::string, int, std::less<>> g_anon_uses;
std::vector<int> g_lua_subs;
std::vector<std::string> g_lua_cmds;

void anon_release(std::string_view action) {
    const auto it = g_anon_uses.find(action);
    if(it == g_anon_uses.end()) return;
    if(--it->second > 0) return;
    g_anon_uses.erase(it);
    registry().remove_action(action);
}

// ---- winter.keymap ---------------------------------------------------------

// Descriptions given with {desc=...}, by (buffer id or 0 for global, mode, keys)
using MapKey = std::tuple<uint64_t, int, KeySeq>;
std::map<MapKey, std::string> g_map_desc;

bool buffer_exists(uint64_t id) {
    return std::ranges::any_of(
        E.buffers, [&](const auto& b) { return b->id == id; });
}

// A buffer's own maps go when it closes. Subscribes once, on the first local
// map; the subscription outlives config reloads (the maps themselves don't).
void wire_local_cleanup() {
    static bool wired = false;
    if(wired) return;
    wired = true;
    events_on("buf_close", [](const std::vector<std::string>&) {
        const Buffer* b = events_buf();
        if(!b) return;
        for(const std::string& a : registry().drop_local(b->id)) anon_release(a);
        std::erase_if(g_map_desc, [&](const auto& kv) {
            return std::get<0>(kv.first) == b->id;
        });
    });
}

// Only these modes look a buffer's own maps up (normal.cpp and the insert chord
// pass the buffer id; the command line, files tree and terminal don't).
bool local_mode_ok(EditorMode m) {
    return m == NORMAL || m == INSERT || m == REPLACE || m == VISUAL;
}

char mode_letter(EditorMode m) {
    switch(m) {
        case NORMAL: return 'n';
        case INSERT: return 'i';
        case REPLACE: return 'r';
        case VISUAL: return 'v';
        case COMMAND: return 'c';
        case SEARCH: return 's';
        case FILES: return 'f';
        case TERMINAL: return 't';
        default: return '?';
    }
}

// opts.buffer: true / 0 = the current buffer, N = buffer id N, false/nil = the
// global map. Returns false for an id that does not exist.
bool read_buffer_opt(lua_State* L, int t, bool& local, uint64_t& buf) {
    local = false;
    buf = 0;
    if(!t) return true;
    lua_getfield(L, t, "buffer");
    bool ok = true;
    if(lua_isboolean(L, -1)) {
        local = lua_toboolean(L, -1) != 0;
        if(local) buf = E.buf().id;
    } else if(lua_isnumber(L, -1)) {
        local = true;
        const lua_Integer id = lua_tointeger(L, -1);
        buf = id == 0 ? E.buf().id : static_cast<uint64_t>(id);
        ok = buffer_exists(buf);
    } else if(!lua_isnil(L, -1)) {
        ok = false;
    }
    lua_pop(L, 1);
    return ok;
}

// winter.keymap.set(modes, keys, action_name | fn [, opts])
// modes: letters from n i r v x c s f ("nx" = normal + visual), a word
// ("normal"), or a table of either: {"n", "x"} / {"normal", "visual"}.
// opts: wants_char = bool; desc = "text" (shown by keymap.list);
//       buffer = true | id: map in that buffer only, shadowing the global map
//       (modes n i r v only; gone when the buffer closes).
int l_keymap_set(lua_State* L) {
    const bool is_fn = lua_isfunction(L, 3);
    if(!is_fn) luaL_checkstring(L, 3);
    const int t = lua_table_arg(L, 4);
    const bool wants_char = lua_opt_bool(L, t, "wants_char", false);
    bool local;
    uint64_t buf;
    if(!read_buffer_opt(L, t, local, buf))
        return luaL_argerror(L, 4, "buffer: true or the id of an open buffer");
    Modes modes;
    if(!collect_modes(L, 1, modes)) return luaL_argerror(L, 1, kModesHelp);
    if(local)
        for(int i = 0; i < modes.n; ++i)
            if(!local_mode_ok(modes.m[i]))
                return luaL_argerror(
                    L, 1, "buffer-local maps work in modes n i r v");
    const char* err = nullptr;
    const KeySeq k = parse_keys(luaL_checkstring(L, 2), &err);
    if(k.empty()) return luaL_argerror(L, 2, err ? err : "empty key sequence");
    std::string desc;
    if(t) {
        lua_getfield(L, t, "desc");
        if(lua_type(L, -1) == LUA_TSTRING) desc = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    std::string action;
    if(is_fn) {
        action = std::format("lua.anon{}", g_next_anon++);
        registry().add_action(action, make_action(lua_fn(L, 3)), wants_char);
    } else {
        action = lua_tostring(L, 3);
    }
    for(int i = 0; i < modes.n; ++i) {
        const Hit old = local ? registry().lookup_local(buf, modes.m[i], k)
                              : registry().lookup(modes.m[i], k);
        if(local) registry().map_local(buf, modes.m[i], k, action);
        else registry().map(modes.m[i], k, action);
        if(old.kind == Lookup::Exact) anon_release(old.action);
        if(is_fn) ++g_anon_uses[action];
        const MapKey key{buf, static_cast<int>(modes.m[i]), k};
        if(desc.empty()) g_map_desc.erase(key);
        else g_map_desc[key] = desc;
    }
    if(local) wire_local_cleanup();
    return 0;
}

// winter.keymap.del(modes, keys [, {buffer=true|id}]) -> true if anything was
// mapped. With buffer: removes that buffer's own map (the global one shows
// through again).
int l_keymap_del(lua_State* L) {
    Modes modes;
    if(!collect_modes(L, 1, modes)) return luaL_argerror(L, 1, kModesHelp);
    const int t = lua_table_arg(L, 3);
    bool local;
    uint64_t buf;
    if(!read_buffer_opt(L, t, local, buf))
        return luaL_argerror(L, 3, "buffer: true or the id of an open buffer");
    bool any = false;
    {
        const char* err = nullptr;
        const KeySeq k = parse_keys(luaL_checkstring(L, 2), &err);
        if(k.empty())
            return luaL_argerror(L, 2, err ? err : "empty key sequence");
        for(int i = 0; i < modes.n; ++i) {
            const Hit h = local ? registry().lookup_local(buf, modes.m[i], k)
                                : registry().lookup(modes.m[i], k);
            const bool removed = local ? registry().unmap_local(buf, modes.m[i], k)
                                       : registry().unmap(modes.m[i], k);
            if(!removed) continue;
            any = true;
            g_map_desc.erase(MapKey{buf, static_cast<int>(modes.m[i]), k});
            if(h.kind == Lookup::Exact) anon_release(h.action);
        }
    }
    lua_pushboolean(L, any);
    return 1;
}

// winter.keymap.list([{mode=, buffer=}]) -> { {mode, keys, action, desc, buffer}, ... }
// Without `buffer`: the global maps (built-ins included). With buffer = true |
// id: that buffer's own maps. mode filters like keymap.set (default: all).
// `keys` is notation ("<C-r>gg"); `desc` and `buffer` are only present when
// there is one.
int l_keymap_list(lua_State* L) {
    const int t = lua_table_arg(L, 1);
    Modes modes;
    bool have_modes = false;
    if(t) {
        lua_getfield(L, t, "mode");
        if(!lua_isnil(L, -1)) {
            if(!collect_modes(L, -1, modes)) {
                lua_pop(L, 1);
                return luaL_argerror(L, 1, kModesHelp);
            }
            have_modes = true;
        }
        lua_pop(L, 1);
    }
    bool local;
    uint64_t buf;
    if(!read_buffer_opt(L, t, local, buf))
        return luaL_argerror(L, 1, "buffer: true or the id of an open buffer");
    if(!have_modes) parse_modes("nirvcsft", modes);
    lua_newtable(L);
    lua_Integer n = 1;
    for(int i = 0; i < modes.n; ++i) {
        const EditorMode m = modes.m[i];
        const auto entries =
            local ? registry().local_maps(buf) : registry().maps(m);
        for(const auto& e : entries) {
            if(e.mode != m) continue;
            lua_createtable(L, 0, 5);
            const char letter = mode_letter(e.mode);
            lua_pushlstring(L, &letter, 1);
            lua_setfield(L, -2, "mode");
            const std::string keys = format_keys(e.keys);
            lua_pushlstring(L, keys.data(), keys.size());
            lua_setfield(L, -2, "keys");
            lua_pushlstring(L, e.action.data(), e.action.size());
            lua_setfield(L, -2, "action");
            if(local) {
                lua_pushinteger(L, static_cast<lua_Integer>(buf));
                lua_setfield(L, -2, "buffer");
            }
            const auto d =
                g_map_desc.find(MapKey{buf, static_cast<int>(e.mode), e.keys});
            if(d != g_map_desc.end()) {
                lua_pushlstring(L, d->second.data(), d->second.size());
                lua_setfield(L, -2, "desc");
            }
            lua_rawseti(L, -2, n++);
        }
    }
    return 1;
}

// ---- winter.action ---------------------------------------------------------

// winter.action.add(name, fn [, {wants_char=bool}])
int l_action_add(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const int t = lua_table_arg(L, 3);
    const bool wants_char = lua_opt_bool(L, t, "wants_char", false);
    registry().add_action(name, make_action(lua_fn(L, 2)), wants_char);
    return 0;
}

struct ActionStep {
    std::string name;
    int count = 0;
    std::string arg;
};

// {count=, arg=} on the table at absolute index `t`; absent keys leave `s` as
// is
void read_step_opts(lua_State* L, int t, ActionStep& s) {
    lua_getfield(L, t, "count");
    if(lua_isinteger(L, -1)) s.count = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, t, "arg");
    if(lua_type(L, -1) == LUA_TSTRING) s.arg = lua_tostring(L, -1);
    lua_pop(L, 1);
}

// winter.action.run(name [, {count=, arg=}]) -> false if there is no such
// action
//
// winter.action.run({step, ...} [, {count=, arg=}]) -> true
//                                | false, failed_name, failed_index
// runs the steps in order. A step is an action name or
// {name, count=, arg=}; the second argument gives the defaults for steps
// that don't set their own. Every name is checked before the first step
// runs, so a typo never leaves a half-run chain.
int l_action_run(lua_State* L) {
    const bool chain = lua_istable(L, 1);
    if(!chain) luaL_checkstring(L, 1);
    bool ok = true, bad = false;
    int failed = 0;
    {
        ActionStep base;
        if(lua_istable(L, 2)) read_step_opts(L, 2, base);
        std::vector<ActionStep> steps;
        std::string msg;
        if(!chain) {
            base.name = lua_tostring(L, 1);
            steps.push_back(std::move(base));
        } else {
            const int n = static_cast<int>(lua_rawlen(L, 1));
            for(int i = 1; i <= n && msg.empty(); ++i) {
                ActionStep s = base;
                s.name.clear();
                lua_rawgeti(L, 1, i);
                if(lua_type(L, -1) == LUA_TSTRING) {
                    s.name = lua_tostring(L, -1);
                } else if(lua_istable(L, -1)) {
                    lua_rawgeti(L, -1, 1);
                    if(lua_type(L, -1) == LUA_TSTRING)
                        s.name = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    read_step_opts(L, lua_gettop(L), s);
                }
                lua_pop(L, 1);
                if(s.name.empty())
                    msg = std::format(
                        "winter.action.run: step {} must be an action name "
                        "or {{name, count=, arg=}}",
                        i);
                else steps.push_back(std::move(s));
            }
        }
        if(!msg.empty()) {
            lua_pushstring(L, msg.c_str());
            bad = true;
        } else {
            const std::vector<std::string> known = registry().action_names();
            for(size_t i = 0; i < steps.size() && ok; ++i)
                if(std::ranges::find(known, steps[i].name) == known.end()) {
                    ok = false;
                    failed = static_cast<int>(i);
                }
            for(size_t i = 0; i < steps.size() && ok; ++i) {
                Ctx ctx{steps[i].count, steps[i].arg};
                if(!registry().run_action(steps[i].name, ctx)) {
                    ok = false;
                    failed = static_cast<int>(i);
                }
            }
            if(!ok && chain) {
                lua_pushboolean(L, 0);
                lua_pushlstring(
                    L, steps[failed].name.data(), steps[failed].name.size());
                lua_pushinteger(L, failed + 1);
            }
        }
    }  // std:: objects are gone before anything can longjmp
    if(bad) return lua_error(L);
    if(!ok && chain) return 3;
    lua_pushboolean(L, ok);
    return 1;
}

int l_action_list(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for(const std::string& n : registry().action_names()) {
        lua_pushlstring(L, n.data(), n.size());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// ---- winter.cmd ------------------------------------------------------------

// winter.cmd.add(Name, fn [, opts]): defines :Name. fn gets
// {args=, bang=, range=, line1=, line2=}; names are letters only.
// opts: desc = "...", bang = bool, range = bool, aliases = {"N", ...},
//       nargs = "0" | "1" | "?" | "*" (default "*"),
//       complete = fn(lead) -> { "text" | {text=, detail=}, ... }
int l_cmd_add(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    luaL_argcheck(L, *name, 1, "empty command name");
    for(const char* p = name; *p; ++p)
        luaL_argcheck(L,
                      std::isalpha(static_cast<unsigned char>(*p)),
                      1,
                      "command names are letters only");
    const int t = lua_table_arg(L, 3);
    Nargs nargs = Nargs::Any;
    if(t) {
        lua_getfield(L, t, "nargs");
        const char* n =
            lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
        if(n) {
            if(!std::strcmp(n, "0")) nargs = Nargs::None;
            else if(!std::strcmp(n, "1")) nargs = Nargs::One;
            else if(!std::strcmp(n, "?")) nargs = Nargs::Opt;
            else if(!std::strcmp(n, "*")) nargs = Nargs::Any;
            else
                return luaL_argerror(
                    L, 3, "nargs is \"0\", \"1\", \"?\" or \"*\"");
        }
        lua_pop(L, 1);
    }
    CmdSpec spec;
    spec.name = name;
    spec.range = lua_opt_bool(L, t, "range", true);
    spec.bang = lua_opt_bool(L, t, "bang", true);
    spec.nargs = nargs;
    spec.raw = true;
    if(t) {
        lua_getfield(L, t, "desc");
        if(lua_type(L, -1) == LUA_TSTRING) spec.desc = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, t, "aliases");
        if(lua_istable(L, -1)) {
            const lua_Unsigned n = lua_rawlen(L, -1);
            for(lua_Unsigned i = 1; i <= n; ++i) {
                lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
                if(lua_type(L, -1) == LUA_TSTRING)
                    spec.aliases.emplace_back(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, t, "complete");
        if(lua_isfunction(L, -1))
            spec.complete = [fn = lua_fn(L, -1)](std::string_view lead) {
                std::vector<CmdCand> out;
                lua_State* L = script_state();
                if(!L) return out;
                const int top = lua_gettop(L);
                fn->push();
                lua_pushlstring(L, lead.data(), lead.size());
                if(script_call(1, 1) && lua_istable(L, top + 1)) {
                    const lua_Unsigned n = lua_rawlen(L, top + 1);
                    for(lua_Unsigned i = 1; i <= n; ++i) {
                        lua_rawgeti(L, top + 1, static_cast<lua_Integer>(i));
                        CmdCand c;
                        if(lua_type(L, -1) == LUA_TSTRING) {
                            c.text = lua_tostring(L, -1);
                        } else if(lua_istable(L, -1)) {
                            lua_getfield(L, -1, "text");
                            if(lua_type(L, -1) == LUA_TSTRING)
                                c.text = lua_tostring(L, -1);
                            lua_pop(L, 1);
                            lua_getfield(L, -1, "detail");
                            if(lua_type(L, -1) == LUA_TSTRING)
                                c.detail = lua_tostring(L, -1);
                            lua_pop(L, 1);
                        }
                        lua_pop(L, 1);
                        if(!c.text.empty()) out.push_back(std::move(c));
                    }
                }
                lua_settop(L, top);
                return out;
            };
        lua_pop(L, 1);
    }
    spec.run = [fn = lua_fn(L, 2)](const CmdLine& c) {
        lua_State* L = script_state();
        if(!L) return;
        fn->push();
        lua_createtable(L, 0, 5);
        lua_pushlstring(L, c.args.data(), c.args.size());
        lua_setfield(L, -2, "args");
        lua_pushboolean(L, c.bang);
        lua_setfield(L, -2, "bang");
        lua_pushboolean(L, c.has_range);
        lua_setfield(L, -2, "range");
        lua_pushinteger(L, c.first);
        lua_setfield(L, -2, "line1");
        lua_pushinteger(L, c.last);
        lua_setfield(L, -2, "line2");
        script_call(1);
    };
    g_lua_cmds.push_back(spec.name);
    command_add(std::move(spec));
    return 0;
}

// winter.cmd.run("s/a/b/")
int l_cmd_run(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    editor_command_palette(std::string_view(s, n));
    return 0;
}

// ---- winter.ext ------------------------------------------------------------

size_t off_in(const PieceTable& t, lua_Integer line, lua_Integer col) {
    const lua_Integer n = static_cast<lua_Integer>(t.line_count());
    const size_t l =
        static_cast<size_t>(std::clamp<lua_Integer>(line, 1, n) - 1);
    const size_t ls = t.line_start(l);
    return std::min(ls + static_cast<size_t>(std::max<lua_Integer>(col - 1, 0)),
                    t.line_end(l));
}

// fn(line, col, count, arg) -> line, col [, "exclusive"|"inclusive"|"linewise"]
// returning nil means the motion failed
motion::Fn motion_from_lua(LuaFn fn) {
    return [fn](const PieceTable& t,
                size_t pos,
                int count,
                std::string_view arg) -> Target {
        Target out;
        out.pos = pos;
        out.ok = false;
        lua_State* L = script_state();
        if(!L) return out;
        const int top = lua_gettop(L);
        fn->push();
        const size_t l = t.line_of(pos);
        lua_pushinteger(L, static_cast<lua_Integer>(l + 1));
        lua_pushinteger(L, static_cast<lua_Integer>(pos - t.line_start(l) + 1));
        lua_pushinteger(L, count);
        lua_pushlstring(L, arg.data(), arg.size());
        if(script_call(4, 3) && lua_isnumber(L, top + 1) &&
           lua_isnumber(L, top + 2)) {
            out.pos =
                off_in(t, lua_tointeger(L, top + 1), lua_tointeger(L, top + 2));
            out.ok = true;
            if(lua_type(L, top + 3) == LUA_TSTRING) {
                const std::string_view k = lua_tostring(L, top + 3);
                if(k == "inclusive") out.kind = MKind::Inclusive;
                else if(k == "linewise") out.kind = MKind::Linewise;
            }
        }
        lua_settop(L, top);
        return out;
    };
}

// fn(line, col, count) -> line1, col1, line2, col2 (end exclusive) | nil
ObjectFn object_from_lua(LuaFn fn) {
    return [fn](const PieceTable& t,
                size_t pos,
                int count) -> std::optional<Range> {
        lua_State* L = script_state();
        if(!L) return std::nullopt;
        const int top = lua_gettop(L);
        fn->push();
        const size_t l = t.line_of(pos);
        lua_pushinteger(L, static_cast<lua_Integer>(l + 1));
        lua_pushinteger(L, static_cast<lua_Integer>(pos - t.line_start(l) + 1));
        lua_pushinteger(L, count);
        std::optional<Range> out;
        if(script_call(3, 4) && lua_isnumber(L, top + 1) &&
           lua_isnumber(L, top + 2) && lua_isnumber(L, top + 3) &&
           lua_isnumber(L, top + 4)) {
            const size_t a =
                off_in(t, lua_tointeger(L, top + 1), lua_tointeger(L, top + 2));
            const size_t b =
                off_in(t, lua_tointeger(L, top + 3), lua_tointeger(L, top + 4));
            if(b > a) out = Range{a, b, false};
        }
        lua_settop(L, top);
        return out;
    };
}

// winter.ext.operator(name, fn, keys): fn gets {line1,col1,line2,col2,linewise}
// and edits through winter.buf. Works as `<keys><motion>`, `<keys><keys>` for
// whole lines and on a visual selection; one undo step.
int l_ext_operator(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const char* keys = luaL_checkstring(L, 3);
    ext_operator(
        name,
        [fn = lua_fn(L, 2)](const Range& r) {
            lua_State* L = script_state();
            if(!L) return;
            fn->push();
            script_push_range(L, r);
            script_call(1);
        },
        keys);
    return 0;
}

// winter.ext.motion(name, fn, keys [, {wants_char=bool}]). fn must not edit.
int l_ext_motion(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const char* keys = luaL_checkstring(L, 3);
    const int t = lua_table_arg(L, 4);
    const bool wants_char = lua_opt_bool(L, t, "wants_char", false);
    ext_motion(name, motion_from_lua(lua_fn(L, 2)), keys, wants_char);
    return 0;
}

// winter.ext.object(name, fn, keys). fn must not edit.
int l_ext_object(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const char* keys = luaL_checkstring(L, 3);
    ext_object(name, object_from_lua(lua_fn(L, 2)), keys);
    return 0;
}

// ---- winter.ev -------------------------------------------------------------

// names for the positional args of the built-in events
// payload fields delivered as Lua numbers; every other field is a string
bool is_numeric_field(std::string_view f) {
    return f == "win" || f == "first_line" || f == "first_col" ||
           f == "last_line" || f == "last_col";
}

const std::vector<const char*>& fields_for(std::string_view ev) {
    static const std::map<std::string, std::vector<const char*>, std::less<>>
        known = {{"buf_enter", {"filename"}},
                 {"mode_changed", {"old", "new"}},
                 {"buf_write_pre", {"filename"}},
                 {"buf_write_post", {"filename"}},
                 {"buf_close", {"filename"}},
                 {"key", {"key"}},
                 {"win_new", {"win"}},
                 {"win_close", {"win"}},
                 {"win_enter", {"win"}},
                 {"win_leave", {"win"}},
                 {"buf_new", {"filename"}},
                 {"yank_post",
                  {"regname", "kind", "first_line", "first_col", "last_line",
                   "last_col"}},
                 {"lsp_attach", {"server"}},
                 {"lsp_detach", {"server"}},
                 {"file_type", {"filename", "lang"}}};
    static const std::vector<const char*> none;
    const auto it = known.find(ev);
    return it == known.end() ? none : it->second;
}

// {pattern = "x" | {"x", "y"}}: false on a bad shape
bool read_patterns(lua_State* L, int t, std::vector<std::string>& out) {
    if(!t) return true;
    lua_getfield(L, t, "pattern");
    bool ok = true;
    if(lua_type(L, -1) == LUA_TSTRING) {
        out.emplace_back(lua_tostring(L, -1));
    } else if(lua_type(L, -1) == LUA_TTABLE) {
        const lua_Unsigned n = lua_rawlen(L, -1);
        for(lua_Unsigned i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            if(lua_type(L, -1) == LUA_TSTRING)
                out.emplace_back(lua_tostring(L, -1));
            else ok = false;
            lua_pop(L, 1);
        }
    } else if(!lua_isnil(L, -1)) {
        ok = false;
    }
    lua_pop(L, 1);
    return ok;
}

// file_type: pattern is a language name; every other event with a leading
// filename: a glob matched against the full path and the basename
bool pattern_hit(std::string_view ev, const std::vector<std::string>& pats,
                 const std::vector<std::string>& args) {
    if(pats.empty()) return true;
    if(args.empty()) return false;
    if(ev == "file_type") {
        if(args.size() < 2) return false;
        return std::ranges::find(pats, args[1]) != pats.end();
    }
    const std::string& f = args[0];
    const size_t sl = f.rfind('/');
    const std::string base = sl == std::string::npos ? f : f.substr(sl + 1);
    for(const std::string& p : pats)
        if(fnmatch(p.c_str(), f.c_str(), 0) == 0 ||
           fnmatch(p.c_str(), base.c_str(), 0) == 0)
            return true;
    return false;
}

// winter.ev.on(event, fn [, {once=bool, pattern=...}]) -> id. fn gets {event=,
// [1]=, ...} plus named fields: buf_enter/buf_write_pre/buf_write_post
// {filename}, buf_close {filename} (the buffer is still valid inside the
// handler), file_type {filename, lang}, mode_changed {old, new}, key {key}
// (every typed key, before it is handled, as "<C-r>" notation; keys sent by
// winter.normal and dot-repeat are not reported; pattern = globs on that
// notation), win_new/win_close/win_enter/win_leave {win} (the window id, a
// number; win_close fires while the window still exists), buf_new {filename}
// (a buffer joined the list, loaded, not yet entered), yank_post {regname, kind
// "char"|"line", first_line, first_col, last_line, last_col} (the yanked range,
// 1-based, end column exclusive; regname "" for the default register),
// lsp_attach / lsp_detach {server} (a language server took / dropped the buffer).
// Also emitted
// without arguments: ready, quit, text_changed, cursor_moved, reload. Every
// payload also has buf = the stable id of the buffer it is about. pattern =
// "lua" | {"lua", "nix"}: file_type runs only for those languages;
// buf_enter/buf_write_*/buf_reload only for files matching those globs.
// Inside the handler winter.opt_local targets the buffer the event is about.
int l_ev_on(lua_State* L) {
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const int t = lua_table_arg(L, 3);
    const bool once = lua_opt_bool(L, t, "once", false);
    const std::string_view ev = event;
    const bool argless = ev == "ready" || ev == "quit" ||
                         ev == "text_changed" || ev == "cursor_moved" ||
                         ev == "reload";
    const char* err = nullptr;  // raised after the block: no C++ objects live
    int new_id = 0;             // across luaL_error
    {
        std::vector<std::string> pats;
        if(!read_patterns(L, t, pats))
            err = "pattern must be a string or a list of strings";
        else if(!pats.empty() && argless)
            err = "this event has no pattern to match";
        else {
            const auto id = std::make_shared<int>(0);
            *id = events_on(
                event,
                [fn = lua_fn(L, 2),
                 id,
                 once,
                 pats = std::move(pats),
                 name =
                     std::string(event)](const std::vector<std::string>& args) {
                    if(!pattern_hit(name, pats, args)) return;
                    if(once) events_off(*id);
                    lua_State* L = script_state();
                    if(!L) return;
                    fn->push();
                    lua_createtable(L, static_cast<int>(args.size()), 3);
                    lua_pushlstring(L, name.data(), name.size());
                    lua_setfield(L, -2, "event");
                    if(const Buffer* eb = events_buf() ? events_buf() : &E.buf()) {
                        lua_pushinteger(L, static_cast<lua_Integer>(eb->id));
                        lua_setfield(L, -2, "buf");  // stable id, see winter.api
                    }
                    const auto& names = fields_for(name);
                    for(size_t i = 0; i < args.size(); ++i) {
                        lua_pushlstring(L, args[i].data(), args[i].size());
                        if(i < names.size()) {
                            if(is_numeric_field(names[i])) {
                                // ids, lines and columns are numbers: ev.win, ev.first_line
                                lua_pushinteger(L, std::atoll(args[i].c_str()));
                            } else {
                                lua_pushvalue(L, -1);
                            }
                            lua_setfield(L, -3, names[i]);
                        }
                        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                    }
                    script_call(1);
                });
            g_lua_subs.push_back(*id);
            new_id = *id;
        }
    }
    if(err) return luaL_error(L, "%s", err);
    lua_pushinteger(L, new_id);
    return 1;
}

int l_ev_off(lua_State* L) {
    lua_pushboolean(L, events_off(static_cast<int>(luaL_checkinteger(L, 1))));
    return 1;
}

// winter.ev.emit(name, ...): custom events for scripts to talk to each other.
// Arguments are strings or numbers.
int l_ev_emit(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const int n = lua_gettop(L);
    bool bad = false;
    {
        std::vector<std::string> args;
        for(int i = 2; i <= n; ++i) {
            const int ty = lua_type(L, i);
            if(ty != LUA_TSTRING && ty != LUA_TNUMBER) {
                bad = true;
                break;
            }
            size_t len;
            const char* s = lua_tolstring(L, i, &len);
            args.emplace_back(s, len);
        }
        if(!bad) events_emit(name, args);
    }
    if(bad) return luaL_error(L, "event arguments must be strings or numbers");
    return 0;
}

// ---- winter.errparser ------------------------------------------------------
// add(name, fn(line, state) -> table|nil)
//   state is a table private to one run, for parsers that span lines.
// add(name, {pattern=, fields=, kind=, type=})
//   pattern is a Lua pattern; its captures fill `fields` in order.
// The table describes one event:
//   {kind="diag", file=, line=, col=, end_col=, type="error"|"warning"|"note",
//    text=, code=}   (kind defaults to "diag"; file is required for it)
//   {kind="context", text=, col=, end_col=}   {kind="progress"|"summary",
//   text=} {kind="test", status="pass"|"fail"|"skip", name=}
// use({names...}) sets the parsers :makeprg runs through.

std::vector<std::string> g_lua_parsers;

std::string tbl_str(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    std::string s;
    if(lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER)
        s = lua_tostring(L, -1);
    lua_pop(L, 1);
    return s;
}

int tbl_int(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    const int v =
        lua_isnumber(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 1);
    return v;
}

char sev_char(const std::string& s) {
    if(s.starts_with('w')) return 'w';
    if(s.starts_with('n') || s.starts_with('i') || s.starts_with('h'))
        return 'n';
    return 'e';
}

ErrEvent event_from_table(lua_State* L, int t, const std::string& dkind,
                          const std::string& dtype) {
    t = lua_absindex(L, t);
    if(!lua_istable(L, t)) return {};
    std::string kind = tbl_str(L, t, "kind");
    if(kind.empty()) kind = dkind.empty() ? "diag" : dkind;
    using K = ErrEvent::Kind;
    if(kind == "context") {
        ErrEvent v = ErrEvent::of_text(K::Context, tbl_str(L, t, "text"));
        v.col = tbl_int(L, t, "col");
        v.end_col = tbl_int(L, t, "end_col");
        return v;
    }
    if(kind == "progress")
        return ErrEvent::of_text(K::Progress, tbl_str(L, t, "text"));
    if(kind == "summary")
        return ErrEvent::of_text(K::Summary, tbl_str(L, t, "text"));
    if(kind == "test") {
        const std::string st = tbl_str(L, t, "status");
        return ErrEvent::of_test(st.starts_with('f')   ? ErrTest::Status::Fail
                                 : st.starts_with('s') ? ErrTest::Status::Skip
                                                       : ErrTest::Status::Pass,
                                 tbl_str(L, t, "name"));
    }
    QfEntry e;
    e.file = tbl_str(L, t, "file");
    if(e.file.empty()) return {};
    e.line = tbl_int(L, t, "line");
    e.col = tbl_int(L, t, "col");
    e.end_col = tbl_int(L, t, "end_col");
    std::string type = tbl_str(L, t, "type");
    e.type = sev_char(type.empty() ? dtype : type);
    e.text = tbl_str(L, t, "text");
    e.code = tbl_str(L, t, "code");
    e.source = "lua";
    return ErrEvent::of_diag(std::move(e));
}

class LuaParser final : public ErrParser {
public:
    explicit LuaParser(LuaFn fn)
        : fn_(std::move(fn)) {
        if(lua_State* L = script_state()) {
            lua_newtable(L);
            state_ = lua_fn(L, -1);
            lua_pop(L, 1);
        }
    }
    LuaParser(std::string pat, std::vector<std::string> fields,
              std::string kind, std::string type)
        : pat_(std::move(pat)),
          fields_(std::move(fields)),
          kind_(std::move(kind)),
          type_(std::move(type)) {}

    ErrEvent feed(std::string_view l) override {
        lua_State* L = script_state();
        if(!L) return {};
        const int base = lua_gettop(L);
        ErrEvent out;
        if(fn_) {
            fn_->push();
            lua_pushlstring(L, l.data(), l.size());
            state_->push();
            if(script_call(2, 1)) out = event_from_table(L, -1, "", "error");
        } else {
            lua_getglobal(L, "string");
            lua_getfield(L, -1, "match");
            lua_remove(L, -2);
            lua_pushlstring(L, l.data(), l.size());
            lua_pushlstring(L, pat_.data(), pat_.size());
            const int nf = static_cast<int>(fields_.size());
            const int top = lua_gettop(L) - 3;  // results start above this
            if(script_call(2, nf) && !lua_isnil(L, top + 1)) {
                lua_newtable(L);
                for(int i = 0; i < nf; ++i) {
                    lua_pushvalue(L, top + 1 + i);
                    lua_setfield(
                        L, -2, fields_[static_cast<size_t>(i)].c_str());
                }
                out = event_from_table(L, -1, kind_, type_);
            }
        }
        lua_settop(L, base);
        return out;
    }

private:
    LuaFn fn_, state_;
    std::string pat_;
    std::vector<std::string> fields_;
    std::string kind_, type_;
};

int l_errparser_add(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    luaL_argcheck(L, !name.empty(), 1, "empty parser name");
    if(lua_isfunction(L, 2)) {
        LuaFn fn = lua_fn(L, 2);
        errparser_register(name,
                           [fn] { return std::make_unique<LuaParser>(fn); });
    } else {
        luaL_checktype(L, 2, LUA_TTABLE);
        const std::string pat = tbl_str(L, 2, "pattern");
        luaL_argcheck(L, !pat.empty(), 2, "pattern = \"...\" is required");
        std::vector<std::string> fields;
        lua_getfield(L, 2, "fields");
        luaL_argcheck(L,
                      lua_istable(L, -1),
                      2,
                      "fields = {\"file\", \"line\", ...} is required");
        const int n = static_cast<int>(lua_rawlen(L, -1));
        for(int i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            fields.emplace_back(lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        const std::string kind = tbl_str(L, 2, "kind"),
                          type = tbl_str(L, 2, "type");
        errparser_register(name, [=] {
            return std::make_unique<LuaParser>(pat, fields, kind, type);
        });
    }
    g_lua_parsers.push_back(name);
    return 0;
}

int l_errparser_use(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<std::string> names;
    const int n = static_cast<int>(lua_rawlen(L, 1));
    for(int i = 1; i <= n; ++i) {
        lua_rawgeti(L, 1, i);
        if(lua_isstring(L, -1)) names.emplace_back(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    errparser_set_default_chain(std::move(names));
    return 0;
}

// winter.term.toggle() / open() / close() / new() / next() / prev() /
// kill() / float() / bottom(): the :Term subcommands as Lua calls.
constexpr const char* kTermSubs[] = {"toggle",
                                     "focus",
                                     "close",
                                     "new",
                                     "next",
                                     "prev",
                                     "kill",
                                     "float",
                                     "bottom"};

template <size_t I>
int l_term(lua_State*) {
    term_run(kTermSubs[I]);
    return 0;
}

int l_term_toggle(lua_State* L) {
    std::string pos;
    if(lua_istable(L, 1)) {
        lua_getfield(L, 1, "position");
        if(lua_isstring(L, -1)) pos = lua_tostring(L, -1);
        else if(!lua_isnil(L, -1))
            return luaL_argerror(L, 1, "position must be a string");
        lua_pop(L, 1);
    }
    if(!pos.empty() && pos != "float" && pos != "bottom")
        return luaL_argerror(L, 1, "position: \"float\" or \"bottom\"");
    term_toggle(pos);
    return 0;
}

const luaL_Reg kTerm[] = {{"toggle", l_term_toggle},
                          {"open", l_term<1>},
                          {"close", l_term<2>},
                          {"new", l_term<3>},
                          {"next", l_term<4>},
                          {"prev", l_term<5>},
                          {"kill", l_term<6>},
                          {"float", l_term<7>},
                          {"bottom", l_term<8>},
                          {nullptr, nullptr}};
const luaL_Reg kKeymap[] = {{"set", l_keymap_set},
                            {"del", l_keymap_del},
                            {"list", l_keymap_list},
                            {nullptr, nullptr}};
const luaL_Reg kAction[] = {{"add", l_action_add},
                            {"run", l_action_run},
                            {"list", l_action_list},
                            {nullptr, nullptr}};
const luaL_Reg kCmd[] = {
    {"add", l_cmd_add}, {"run", l_cmd_run}, {nullptr, nullptr}};
const luaL_Reg kExt[] = {{"operator", l_ext_operator},
                         {"motion", l_ext_motion},
                         {"object", l_ext_object},
                         {nullptr, nullptr}};
const luaL_Reg kErrparser[] = {
    {"add", l_errparser_add}, {"use", l_errparser_use}, {nullptr, nullptr}};
const luaL_Reg kEv[] = {{"on", l_ev_on},
                        {"off", l_ev_off},
                        {"emit", l_ev_emit},
                        {nullptr, nullptr}};

}  // namespace

void script_open_ext() {
    script_register("keymap", kKeymap);
    script_register("action", kAction);
    script_register("cmd", kCmd);
    script_register("term", kTerm);
    script_register("ext", kExt);
    script_register("ev", kEv);
    script_register("errparser", kErrparser);
}

void script_ext_reset() {
    for(const int id : g_lua_subs) events_off(id);
    g_lua_subs.clear();
    for(const std::string& n : g_lua_cmds) command_remove(n);
    g_lua_cmds.clear();
    for(const std::string& n : g_lua_parsers) errparser_remove(n);
    g_lua_parsers.clear();
    errparser_register_builtin();  // a Lua parser may have replaced a built-in
    register_builtin_commands();
    g_anon_uses.clear();
    g_map_desc.clear();
    registry_restore();
    script_runtime_reset();
}
