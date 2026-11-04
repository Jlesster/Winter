// winter.keymap, winter.action, winter.cmd, winter.ext, winter.ev
#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "command.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "extend.hpp"
#include "motion.hpp"
#include "registry.hpp"
#include "script_internal.hpp"

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
            default: return false;
        }
        if(std::find(out.m, out.m + out.n, m) == out.m + out.n && out.n < 8)
            out.m[out.n++] = m;
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

void anon_release(std::string_view action) {
    const auto it = g_anon_uses.find(action);
    if(it == g_anon_uses.end()) return;
    if(--it->second > 0) return;
    g_anon_uses.erase(it);
    registry().remove_action(action);
}

// ---- winter.keymap ---------------------------------------------------------

// winter.keymap.set(modes, keys, action_name | fn [, {wants_char=bool}])
// modes: letters from n i r v x c s ("nx" = normal + visual).
int l_keymap_set(lua_State* L) {
    size_t mn;
    const char* ms = luaL_checklstring(L, 1, &mn);
    const char* keys = luaL_checkstring(L, 2);
    const bool is_fn = lua_isfunction(L, 3);
    if(!is_fn) luaL_checkstring(L, 3);
    const int t = lua_table_arg(L, 4);
    const bool wants_char = lua_opt_bool(L, t, "wants_char", false);
    Modes modes;
    if(!parse_modes(std::string_view(ms, mn), modes))
        return luaL_argerror(L, 1, "modes are letters from n i r v x c s");
    const std::string k = parse_keys(keys);
    std::string action;
    if(is_fn) {
        action = std::format("lua.anon{}", g_next_anon++);
        registry().add_action(action, make_action(lua_fn(L, 3)), wants_char);
    } else {
        action = lua_tostring(L, 3);
    }
    for(int i = 0; i < modes.n; ++i) {
        const Hit old = registry().lookup(modes.m[i], k);
        registry().map(modes.m[i], k, action);
        if(old.kind == Lookup::Exact) anon_release(old.action);
        if(is_fn) ++g_anon_uses[action];
    }
    return 0;
}

// winter.keymap.del(modes, keys) -> true if anything was mapped
int l_keymap_del(lua_State* L) {
    size_t mn;
    const char* ms = luaL_checklstring(L, 1, &mn);
    const char* keys = luaL_checkstring(L, 2);
    Modes modes;
    if(!parse_modes(std::string_view(ms, mn), modes))
        return luaL_argerror(L, 1, "modes are letters from n i r v x c s");
    bool any = false;
    {
        const std::string k = parse_keys(keys);
        for(int i = 0; i < modes.n; ++i) {
            const Hit h = registry().lookup(modes.m[i], k);
            if(!registry().unmap(modes.m[i], k)) continue;
            any = true;
            if(h.kind == Lookup::Exact) anon_release(h.action);
        }
    }
    lua_pushboolean(L, any);
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

// winter.action.run(name [, {count=, arg=}]) -> false if there is no such
// action
int l_action_run(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    int count = 0;
    const char* arg = "";
    if(lua_istable(L, 2)) {
        lua_getfield(L, 2, "count");
        if(lua_isinteger(L, -1)) count = static_cast<int>(lua_tointeger(L, -1));
        lua_getfield(L, 2, "arg");
        if(lua_type(L, -1) == LUA_TSTRING) arg = lua_tostring(L, -1);
    }
    Ctx ctx{count, arg};
    lua_pushboolean(L, registry().run_action(name, ctx));
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
        const char* n = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
        if(n) {
            if(!std::strcmp(n, "0")) nargs = Nargs::None;
            else if(!std::strcmp(n, "1")) nargs = Nargs::One;
            else if(!std::strcmp(n, "?")) nargs = Nargs::Opt;
            else if(!std::strcmp(n, "*")) nargs = Nargs::Any;
            else return luaL_argerror(L, 3, "nargs is \"0\", \"1\", \"?\" or \"*\"");
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
                            if(lua_type(L, -1) == LUA_TSTRING) c.text = lua_tostring(L, -1);
                            lua_pop(L, 1);
                            lua_getfield(L, -1, "detail");
                            if(lua_type(L, -1) == LUA_TSTRING) c.detail = lua_tostring(L, -1);
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
const std::vector<const char*>& fields_for(std::string_view ev) {
    static const std::map<std::string, std::vector<const char*>, std::less<>>
        known = {{"buf_enter", {"filename"}},
                 {"mode_changed", {"old", "new"}},
                 {"buf_write_pre", {"filename"}},
                 {"buf_write_post", {"filename"}},
                 {"file_type", {"filename", "lang"}}};
    static const std::vector<const char*> none;
    const auto it = known.find(ev);
    return it == known.end() ? none : it->second;
}

// winter.ev.on(event, fn [, {once=bool}]) -> id. fn gets {event=, [1]=, ...}
// plus named fields: buf_enter/buf_write_pre/buf_write_post {filename},
// file_type {filename, lang}, mode_changed {old, new}. Also emitted without
// arguments: ready, quit, text_changed, cursor_moved.
int l_ev_on(lua_State* L) {
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const int t = lua_table_arg(L, 3);
    const bool once = lua_opt_bool(L, t, "once", false);
    const auto id = std::make_shared<int>(0);
    *id = events_on(event,
                    [fn = lua_fn(L, 2), id, once, name = std::string(event)](
                        const std::vector<std::string>& args) {
                        if(once) events_off(*id);
                        lua_State* L = script_state();
                        if(!L) return;
                        fn->push();
                        lua_createtable(L, static_cast<int>(args.size()), 3);
                        lua_pushlstring(L, name.data(), name.size());
                        lua_setfield(L, -2, "event");
                        const auto& names = fields_for(name);
                        for(size_t i = 0; i < args.size(); ++i) {
                            lua_pushlstring(L, args[i].data(), args[i].size());
                            if(i < names.size()) {
                                lua_pushvalue(L, -1);
                                lua_setfield(L, -3, names[i]);
                            }
                            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                        }
                        script_call(1);
                    });
    lua_pushinteger(L, *id);
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

const luaL_Reg kKeymap[] = {
    {"set", l_keymap_set}, {"del", l_keymap_del}, {nullptr, nullptr}};
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
const luaL_Reg kEv[] = {{"on", l_ev_on},
                        {"off", l_ev_off},
                        {"emit", l_ev_emit},
                        {nullptr, nullptr}};

}  // namespace

void script_open_ext() {
    script_register("keymap", kKeymap);
    script_register("action", kAction);
    script_register("cmd", kCmd);
    script_register("ext", kExt);
    script_register("ev", kEv);
}
