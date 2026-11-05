#include "script.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <lua.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "buflist.hpp"
#include "columns.hpp"
#include "command.hpp"
#include "comment.hpp"
#include "core.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "file.hpp"
#include "glyphs.hpp"
#include "inputbox.hpp"
#include "job.hpp"
#include "json.hpp"
#include "jumplist.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "lsp_actions.hpp"
#include "lsp_config.hpp"
#include "lsp_nav.hpp"
#include "lsp_rename.hpp"
#include "lua_json.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "overlay.hpp"
#include "picker.hpp"
#include "registry.hpp"
#include "script_internal.hpp"
#include "snippet.hpp"
#include "statusline.hpp"
#include "syntax.hpp"
#include "theme.hpp"
#include "utf8.hpp"
#include "virt.hpp"

namespace {

lua_State* g_L = nullptr;
std::string g_last_error;

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

// helper to add */themes dirs to lua's require() search path
void prepend_package_path(std::string_view dir) {
    lua_getglobal(g_L, "package");
    lua_getfield(g_L, -1, "path");
    const char* old = lua_tostring(g_L, -1);
    const std::string next =
        std::format("{}/?.lua;{}/?/init.lua;{}", dir, dir, old ? old : "");
    lua_pop(g_L, 1);
    lua_pushlstring(g_L, next.data(), next.size());
    lua_setfield(g_L, -2, "path");
    lua_pop(g_L, 1);
}

// keep a Lua function alive by parking it in Lua's registry
int ref_function(lua_State* L, int idx) {
    lua_pushvalue(L, idx);
    return luaL_ref(L, LUA_REGISTRYINDEX);
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

int l_snippet(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    lua_pushboolean(L, snippet_expand(std::string_view(s, n)));
    return 1;
}

int l_snippet_add(lua_State* L) {
    size_t tn, bn;
    const char* t = luaL_checklstring(L, 1, &tn);
    const char* b = luaL_checklstring(L, 2, &bn);
    snippet_add(std::string(t, tn), std::string(b, bn));
    return 0;
}

int l_status(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    editor_set_status_message("{}", std::string_view(s, n));
    return 0;
}

int l_comment(lua_State* L) {
    const char* lang = luaL_checkstring(L, 1);
    const char* a = luaL_checkstring(L, 2);
    const char* b = luaL_optstring(L, 3, nullptr);
    comment_register(lang,
                     b ? CommentStyle{"", a, b} : CommentStyle{a, "", ""});
    return 0;
}

// winter.tree.icons({ lua = "\u{e620}", zig = "\u{e6a9}" })
int l_tree_icons(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while(lua_next(L, 1)) {
        if(lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING)
            return luaL_error(L, "winter.tree.icons: { ext = \"glyph\", ... }");
        size_t kn, vn;
        const char* k = lua_tolstring(L, -2, &kn);
        const char* v = lua_tolstring(L, -1, &vn);
        tree_icon_set({k, kn}, {v, vn});
        lua_pop(L, 1);
    }
    return 0;
}
const luaL_Reg kTreeLua[] = {{"icons", l_tree_icons}, {nullptr, nullptr}};

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

int l_lsp_goto(lua_State* L) {
    NavKind kind{};
    if(!nav_kind_from_name(luaL_checkstring(L, 1), kind))
        return luaL_argerror(
            L,
            1,
            "definition|declaration|type_definition|implementation|references");
    lsp_goto(kind);
    return 0;
}

int l_lsp_switch(lua_State*) {
    lsp_switch_source_header();
    return 0;
}

int l_lsp_code_action(lua_State* L) {
    lsp_code_action();
    return 0;
}

int l_jump_older(lua_State* L) {
    jump_older(static_cast<int>(luaL_optinteger(L, 1, 1)));
    return 0;
}

int l_jump_newer(lua_State* L) {
    jump_newer(static_cast<int>(luaL_optinteger(L, 1, 1)));
    return 0;
}

#include "script_deco.inc"

bool color_at(lua_State* L, int idx, uint32_t* out) {
    if(lua_isinteger(L, idx)) {
        *out = static_cast<uint32_t>(lua_tointeger(L, idx)) & 0xFFFFFF;
        return true;
    }
    if(lua_type(L, idx) != LUA_TSTRING) return false;
    const char* s = lua_tostring(L, idx);
    if(s[0] != '#' || std::strlen(s) != 7) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s + 1, &end, 16);
    if(*end != '\0') return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

// winter.theme.set(group, {fg=, bg=, ul=, bold=, italic=, ...})
// winter.theme.set(group, false)  -- remove the override, back to the default
int l_theme_set(lua_State* L) {
    size_t n;
    const char* g = luaL_checklstring(L, 1, &n);
    const std::string_view group(g, n);
    if(lua_isnoneornil(L, 2) || (lua_isboolean(L, 2) && !lua_toboolean(L, 2))) {
        theme_unset(group);
        return 0;
    }
    const int t = table_arg(L, 2);
    bool bad = false;
    const Style st = read_style(L, t, &bad);
    if(bad)
        return luaL_error(
            L, "winter.theme.set: colours are 0xRRGGBB or \"#rrggbb\"");
    theme_set(group, st);
    return 0;
}

// winter.theme.apply({ ["function"] = {...}, ["ui.statusline"] = {...} })
int l_theme_apply(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while(lua_next(L, 1)) {
        if(lua_type(L, -2) != LUA_TSTRING)
            return luaL_error(L, "winter.theme.apply: keys are group names");
        size_t n;
        const char* g = lua_tolstring(L, -2, &n);
        const std::string_view group(g, n);
        if(lua_istable(L, -1)) {
            bool bad = false;
            const Style st = read_style(L, lua_gettop(L), &bad);
            if(bad)
                return luaL_error(
                    L, "winter.theme.apply: bad colour in '%s'", g);
            theme_set(group, st);
        } else if(lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
            theme_unset(group);
        } else {
            return luaL_error(
                L, "winter.theme.apply: '%s' needs a table or false", g);
        }
        lua_pop(L, 1);
    }
    return 0;
}

void push_color_field(lua_State* L, const char* key, uint32_t c) {
    if(c == 0xFFFFFFFF) return;
    char b[8];
    std::snprintf(b, sizeof b, "#%06x", c & 0xFFFFFF);
    lua_pushstring(L, b);
    lua_setfield(L, -2, key);
}

void push_flag_field(lua_State* L, const char* key, bool on) {
    if(!on) return;
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, key);
}

// winter.theme.get(group) -> the override table, or nil if none is set
int l_theme_get(lua_State* L) {
    const auto st = theme_override(luaL_checkstring(L, 1));
    if(!st) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    push_color_field(L, "fg", st->fg);
    push_color_field(L, "bg", st->bg);
    push_color_field(L, "ul", st->ul);
    push_flag_field(L, "bold", st->attrs & ATTR_BOLD);
    push_flag_field(L, "dim", st->attrs & ATTR_DIM);
    push_flag_field(L, "italic", st->attrs & ATTR_ITALIC);
    push_flag_field(L, "underline", st->attrs & ATTR_UNDERLINE);
    push_flag_field(L, "undercurl", st->attrs & ATTR_UNDERCURL);
    push_flag_field(L, "reverse", st->attrs & ATTR_REVERSE);
    push_flag_field(L, "strikethrough", st->attrs & ATTR_STRIKE);
    return 1;
}

// winter.theme.reset(): drop every override and the rainbow palette
int l_theme_reset(lua_State*) {
    theme_clear();
    return 0;
}

// winter.theme.rainbow({"#f38ba8", 0xfab387, ...}); no argument = built-in
int l_theme_rainbow(lua_State* L) {
    if(lua_isnoneornil(L, 1)) {
        theme_set_rainbow({});
        return 0;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    const lua_Integer n = luaL_len(L, 1);
    uint32_t c = 0;
    for(lua_Integer i = 1; i <= n; ++i) {  // validate first: nothing to unwind
        lua_rawgeti(L, 1, i);
        const bool ok = color_at(L, -1, &c);
        lua_pop(L, 1);
        if(!ok)
            return luaL_error(L,
                              "winter.theme.rainbow: item %d is not a colour",
                              static_cast<int>(i));
    }
    std::vector<uint32_t> v;
    for(lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, 1, i);
        color_at(L, -1, &c);
        lua_pop(L, 1);
        v.push_back(c);
    }
    theme_set_rainbow(std::move(v));
    return 0;
}

// winter.theme.groups() -> the built-in ui.* / diag.* group names
int l_theme_groups(lua_State* L) {
    lua_newtable(L);
    lua_Integer i = 1;
    for(const std::string_view g : theme_ui_groups()) {
        lua_pushlstring(L, g.data(), g.size());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int l_theme_link(lua_State* L) {
    size_t n, m;
    const char* a = luaL_checklstring(L, 1, &n);
    if(lua_isnoneornil(L, 2) || (lua_isboolean(L, 2) && !lua_toboolean(L, 2))) {
        theme_unlink({a, n});
        return 0;
    }
    const char* b = luaL_checklstring(L, 2, &m);
    theme_link({a, n}, {b, m});
    return 0;
}

#include "script_ui.inc"

const luaL_Reg kBuf[] = {{"replace", l_replace}, {nullptr, nullptr}};
const luaL_Reg kUi[] = {
    {"input", l_input}, {"status", l_status}, {nullptr, nullptr}};
const luaL_Reg kFloat[] = {{"open", l_float},
                           {"put", l_float_put},
                           {"clear", l_float_clear},
                           {"close", l_float_close},
                           {"cursor", l_float_cursor},
                           {nullptr, nullptr}};
const luaL_Reg kDeco[] = {{"ns", l_ns},
                          {"span", l_span},
                          {"eol", l_eol},
                          {"clear", l_clear_ns},
                          {nullptr, nullptr}};
const luaL_Reg kLsp[] = {{"setup", l_lsp},
                         {"goto", l_lsp_goto},
                         {"switch_source_header", l_lsp_switch},
                         {"code_action", l_lsp_code_action},
                         {nullptr, nullptr}};
const luaL_Reg kJump[] = {
    {"older", l_jump_older}, {"newer", l_jump_newer}, {nullptr, nullptr}};
const luaL_Reg kSnippet[] = {
    {"expand", l_snippet}, {"add", l_snippet_add}, {nullptr, nullptr}};
const luaL_Reg kLang[] = {{"comment", l_comment}, {nullptr, nullptr}};
const luaL_Reg kTheme[] = {{"set", l_theme_set},
                           {"apply", l_theme_apply},
                           {"get", l_theme_get},
                           {"reset", l_theme_reset},
                           {"rainbow", l_theme_rainbow},
                           {"groups", l_theme_groups},
                           {"link", l_theme_link},
                           {nullptr, nullptr}};

}  // namespace

lua_State* script_state() { return g_L; }

bool script_call(int nargs, int nresults) { return call(nargs, nresults); }

void script_register(const char* group, const luaL_Reg* funcs) {
    const int base = lua_gettop(g_L);
    lua_getglobal(g_L, "winter");
    if(group) {
        lua_getfield(g_L, -1, group);
        if(!lua_istable(g_L, -1)) {
            lua_pop(g_L, 1);
            lua_newtable(g_L);
            lua_pushvalue(g_L, -1);
            lua_setfield(g_L, -3, group);
        }
    }
    luaL_setfuncs(g_L, funcs, 0);
    lua_settop(g_L, base);
}

bool script_init() {
    g_L = luaL_newstate();
    if(!g_L) return false;
    luaL_openlibs(g_L);
    prepend_package_path("/etc/xdg/winter/themes");
    lua_newtable(g_L);  // winter
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

    script_register("buf", kBuf);
    script_register("ui", kUi);
    script_register("float", kFloat);
    script_register("deco", kDeco);
    script_register("lsp", kLsp);
    script_register("jump", kJump);
    script_register("snippet", kSnippet);
    script_register("lang", kLang);
    script_register("theme", kTheme);
    script_register("tree", kTreeLua);
    script_register("statusline", kStatusline);
    script_register("buf", kBufExt);
    script_register("win", kWin);
    script_register("job", kJob);
    script_register("timer", kTimer);
    script_register("picker", kPicker);
    script_register("ui", kUiExt);
    script_register("diag", kDiag);
    script_register("options", kOptions);
    script_register("lsp", kLspExt);
    script_open_buf();
    script_open_ext();

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
    prepend_package_path((fs::path(dir) / "themes").string());
    prepend_package_path(dir);

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
