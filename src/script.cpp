#include "script.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <lua.hpp>
#include <memory>
#include <string>
#include <string_view>
#include "columns.hpp"
#include "command.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "json.hpp"
#include "lsp_config.hpp"
#include "lua_json.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "registry.hpp"
#include "utf8.hpp"

namespace {

lua_State* g_L = nullptr;
std::string g_last_error;
int g_next_anon = 1;

void report(const char* msg) {
    g_last_error = msg ? msg : "(unknown error)";
    const std::string first = g_last_error.substr(0, g_last_error.find('\n'));
    editor_set_status_message("Lua: {}",
                              first);  // one line; full text kept above
}

int traceback_handler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

// the function and its `nargs` arguments are on the stack; call it protected
bool call(int nargs, int nresults = 0) {
    const int base = lua_gettop(g_L) - nargs;
    lua_pushcfunction(g_L, traceback_handler);
    lua_insert(g_L, base);
    const int rc = lua_pcall(g_L, nargs, nresults, base);
    lua_remove(g_L, base);
    if(rc != LUA_OK) {
        report(lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return false;
    }
    return true;
}

// keep a Lua function alive by parking it in Lua's registry
int ref_function(lua_State* L, int idx) {
    lua_pushvalue(L, idx);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

// wrap a Lua function as a C++ Action. It receives {count=, arg=}
Action make_action(int ref) {
    return [ref](Ctx& c) {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, ref);
        lua_createtable(g_L, 0, 2);
        lua_pushinteger(g_L, c.count);
        lua_setfield(g_L, -2, "count");
        lua_pushlstring(g_L, c.arg.data(), c.arg.size());
        lua_setfield(g_L, -2, "arg");
        call(1);
    };
}

bool parse_mode(std::string_view s, EditorMode* out) {
    if(s == "n" || s == "normal") *out = NORMAL;
    else if(s == "i" || s == "insert") *out = INSERT;
    else if(s == "v" || s == "visual") *out = VISUAL;
    else if(s == "c" || s == "command") *out = COMMAND;
    else if(s == "s" || s == "search") *out = SEARCH;
    else return false;
    return true;
}

// tostring() of every argument, tab-separated. Used by print and :lua.
int join_values(lua_State* L) {
    const int n = lua_gettop(L);
    luaL_Buffer b;  // Lua's own buffer: no C++ objects
    luaL_buffinit(L, &b);
    for(int i = 1; i <= n; ++i) {
        if(i > 1) luaL_addchar(&b, '\t');
        luaL_tolstring(L, i, nullptr);  // may call __tostring, which may error
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    return 1;
}

int opt_index(lua_State* L) {
    const char* name = luaL_checkstring(L, 2);
    {
        const auto v = get_option(name);
        if(v) {
            if(*v == "true") lua_pushboolean(L, 1);
            else if(*v == "false") lua_pushboolean(L, 0);
            else if(!v->empty() &&
                    std::isdigit(static_cast<unsigned char>(v->front())))
                lua_pushinteger(L, std::atoll(v->c_str()));
            else lua_pushstring(L, v->c_str());
            return 1;
        }
    }
    return luaL_error(L, "unknown option '%s'", name);
}

int opt_newindex(lua_State* L) {
    const char* name = luaL_checkstring(L, 2);
    const int t = lua_type(L, 3);
    if(t != LUA_TBOOLEAN && t != LUA_TNUMBER && t != LUA_TSTRING)
        return luaL_argerror(L, 3, "expected number, boolean or string");

    OptResult r;
    {
        std::string value;
        if(t == LUA_TBOOLEAN) value = lua_toboolean(L, 3) ? "true" : "false";
        else if(t == LUA_TNUMBER) {
            const double d = lua_tonumber(L, 3);
            value = d == std::floor(d)
                        ? std::to_string(static_cast<long long>(d))
                        : std::to_string(d);
        } else value = lua_tostring(L, 3);
        r = set_option(name, value);
    }
    if(r == OptResult::Unknown)
        return luaL_error(L, "unknown option '%s'", name);
    if(r == OptResult::BadValue) {
        lua_pushvalue(L, 3);
        return luaL_error(L,
                          "invalid value for '%s': '%s'",
                          name,
                          luaL_tolstring(L, -1, nullptr));
    }
    return 0;
}

int l_map(lua_State* L) {
    const char* mode = luaL_checkstring(L, 1);
    const char* keys = luaL_checkstring(L, 2);
    EditorMode m;
    if(!parse_mode(mode, &m)) return luaL_error(L, "unknown mode '%s'", mode);
    if(lua_isfunction(L, 3)) {
        const int ref = ref_function(L, 3);
        std::string name = std::format("lua.anon{}", g_next_anon++);
        registry().add_action(name, make_action(ref));
        registry().map(m, parse_keys(keys), std::move(name));
    } else {
        const char* action = luaL_checkstring(L, 3);
        registry().map(m, parse_keys(keys), action);
    }
    return 0;
}

int l_unmap(lua_State* L) {
    const char* mode = luaL_checkstring(L, 1);
    const char* keys = luaL_checkstring(L, 2);
    EditorMode m;
    if(!parse_mode(mode, &m)) return luaL_error(L, "unknown mode '%s'", mode);
    lua_pushboolean(L, registry().unmap(m, parse_keys(keys)));
    return 1;
}

int l_action(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    bool wants_char = false;
    if(lua_istable(L, 3)) {
        lua_getfield(L, 3, "wants_char");
        wants_char = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    const int ref = ref_function(L, 2);
    registry().add_action(name, make_action(ref), wants_char);
    return 0;
}

int l_run(lua_State* L) {
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

int l_actions(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for(const std::string& n : registry().action_names()) {
        lua_pushlstring(L, n.data(), n.size());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int l_on(lua_State* L) {
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const int ref = ref_function(L, 2);
    const int id =
        events_on(event, [ref](const std::vector<std::string>& args) {
            lua_rawgeti(g_L, LUA_REGISTRYINDEX, ref);
            for(const std::string& a : args)
                lua_pushlstring(g_L, a.data(), a.size());
            call(static_cast<int>(args.size()));
        });
    lua_pushinteger(L, id);
    return 1;
}

int l_off(lua_State* L) {
    lua_pushboolean(L, events_off(static_cast<int>(luaL_checkinteger(L, 1))));
    return 1;
}

int l_cursor(lua_State* L) {
    lua_pushinteger(L, E.cy + 1);
    lua_pushinteger(L, E.cx + 1);
    return 2;
}

void place_cursor(lua_Integer line, lua_Integer col) {
    E.cy = static_cast<int>(
        std::clamp<lua_Integer>(line - 1, 0, E.num_rows() - 1));
    const std::string text = E.buf().pt.line_text(E.cy);
    const int size = static_cast<int>(text.size());
    int cx = snap_cx(
        text, static_cast<int>(std::clamp<lua_Integer>(col - 1, 0, size)));
    if(E.mode != INSERT && cx >= size && size > 0)
        cx = static_cast<int>(utf8_prev(text, static_cast<size_t>(size)));
    E.cx = cx;
    update_want();
}

int l_set_cursor(lua_State* L) {
    const lua_Integer line = luaL_checkinteger(L, 1);
    const lua_Integer col = luaL_optinteger(L, 2, 1);
    place_cursor(line, col);
    return 0;
}

int l_line(lua_State* L) {
    const lua_Integer n = luaL_optinteger(L, 1, E.cy + 1);
    if(n < 1 || n > E.num_rows()) {
        lua_pushnil(L);
        return 1;
    }
    const std::string text = E.buf().pt.line_text(static_cast<size_t>(n - 1));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int l_line_count(lua_State* L) {
    lua_pushinteger(L, E.num_rows());
    return 1;
}

void insert_text(std::string_view text) {
    std::unique_ptr<PieceTable::Group> group;
    if(E.mode != INSERT)
        group = std::make_unique<PieceTable::Group>(E.buf().pt);
    E.buf().pt.insert(cursor_offset(), text);
    const auto newlines = std::count(text.begin(), text.end(), '\n');
    if(newlines == 0) {
        E.cx += static_cast<int>(text.size());
    } else {
        E.cy += static_cast<int>(newlines);
        E.cx = static_cast<int>(text.size() - (text.rfind('\n') + 1));
    }
    if(E.mode != INSERT)
        set_cursor_offset(motion::clamp_normal(E.buf().pt, cursor_offset()));
    update_want();
}

int l_insert(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    insert_text(std::string_view(s, n));
    return 0;
}

int l_mode(lua_State* L) {
    lua_pushstring(L, mode_name(E.mode));
    return 1;
}

int l_filename(lua_State* L) {
    lua_pushlstring(L, E.buf().filename.data(), E.buf().filename.size());
    return 1;
}

int l_status(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    editor_set_status_message("{}", std::string_view(s, n));
    return 0;
}

int l_command(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    editor_command_palette(std::string_view(s, n));
    return 0;
}

int l_print(lua_State* L) {
    const int n = lua_gettop(L);
    lua_pushcfunction(L, join_values);
    lua_insert(L, 1);
    lua_call(L, n, 1);
    editor_set_status_message("{}", lua_tostring(L, -1));
    return 0;
}

int l_lsp(lua_State* L) {
    size_t n = 0;
    const char* name = luaL_checklstring(L, 1, &n);
    if(!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
    bool ok = true;
    {
        Json opts = Json::obj();
        std::string err;
        if(!lua_isnoneornil(L, 2)) lua_to_json(L, 2, opts, err);
        if(err.empty())
            err = lsp_configure(std::string_view(name, n), std::move(opts));
        if(!err.empty()) {
            err = std::format("winter.lsp(\"{}\"): {}", name, err);
            luaL_where(L, 1);
            lua_pushlstring(L, err.data(), err.size());
            lua_concat(L, 2);
            ok = false;
        }
    }
    return ok ? 0 : lua_error(L);
}

const luaL_Reg kApi[] = {{"map", l_map},
                         {"unmap", l_unmap},
                         {"action", l_action},
                         {"run", l_run},
                         {"actions", l_actions},
                         {"on", l_on},
                         {"off", l_off},
                         {"cursor", l_cursor},
                         {"set_cursor", l_set_cursor},
                         {"line", l_line},
                         {"line_count", l_line_count},
                         {"insert", l_insert},
                         {"mode", l_mode},
                         {"filename", l_filename},
                         {"status", l_status},
                         {"command", l_command},
                         {"lsp", l_lsp},
                         {nullptr, nullptr}};

}  // namespace

bool script_init() {
    g_L = luaL_newstate();
    if(!g_L) return false;
    luaL_openlibs(g_L);

    luaL_newlib(g_L, kApi);
    lua_newtable(g_L);
    lua_newtable(g_L);
    lua_pushcfunction(g_L, opt_index);
    lua_setfield(g_L, -2, "__index");
    lua_pushcfunction(g_L, opt_newindex);
    lua_setfield(g_L, -2, "__newindex");
    lua_setmetatable(g_L, -2);
    lua_setfield(g_L, -2, "opt");
    lua_pushstring(g_L, WINTER_VERSION);
    lua_setfield(g_L, -2, "version");
    lua_setglobal(g_L, "winter");

    lua_pushcfunction(g_L, l_print);
    lua_setglobal(g_L, "print");
    return true;
}

void script_shutdown() {
    if(g_L) lua_close(g_L);
    g_L = nullptr;
}

std::string script_config_dir() {
    namespace fs = std::filesystem;
    if(const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x)
        return (fs::path(x) / "winter").string();
    if(const char* h = std::getenv("HOME"); h && *h)
        return (fs::path(h) / ".config" / "winter").string();
    return {};
}

void script_load_config() {
    namespace fs = std::filesystem;
    if(!g_L) return;
    const std::string dir = script_config_dir();
    if(dir.empty()) return;
    const std::string init = (fs::path(dir) / "init.lua").string();
    std::error_code ec;
    if(!fs::exists(init, ec)) return;

    lua_getglobal(g_L, "package");
    lua_getfield(g_L, -1, "path");
    const std::string path = std::format(
        "{}/?.lua;{}/?/init.lua;{}", dir, dir, lua_tostring(g_L, -1));
    lua_pop(g_L, 1);
    lua_pushstring(g_L, path.c_str());
    lua_setfield(g_L, -2, "path");
    lua_pop(g_L, 1);
    if(luaL_loadfile(g_L, init.c_str()) != LUA_OK) {
        report(lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return;
    }
    call(0);
}

std::string script_eval(std::string_view code) {
    if(!g_L) return "Lua is not available";
    const std::string expr = "return " + std::string(code);
    int rc = luaL_loadbuffer(g_L, expr.data(), expr.size(), "=:lua");
    if(rc != LUA_OK) {
        lua_pop(g_L, 1);
        rc = luaL_loadbuffer(g_L, code.data(), code.size(), "=:lua");
    }
    if(rc != LUA_OK) {
        report(lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return "";
    }
    const int before = lua_gettop(g_L) - 1;
    if(!call(0, LUA_MULTRET)) return "";
    lua_pushcfunction(g_L, join_values);
    lua_insert(g_L, before + 1);
    if(!call(lua_gettop(g_L) - (before + 1), 1)) {
        lua_settop(g_L, before);
        return "";
    }
    std::string out = lua_tostring(g_L, -1);
    lua_settop(g_L, before);
    return out;
}

const std::string& script_last_error() { return g_last_error; }
