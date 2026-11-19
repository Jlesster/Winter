#include <lua.hpp>
#include <cstdio>
#include <string>
#include "script_internal.hpp"
#include "tsapi.hpp"
extern "C" const TSLanguage* tree_sitter_c();

static lua_State* g_L; static TSParser* g_parser; static TSTree* g_tree = nullptr; static std::string g_src; static uint64_t g_gen = 1;
lua_State* script_state() { return g_L; }
void script_register(const char* group, const luaL_Reg* funcs) {
    lua_getglobal(g_L, "winter"); lua_getfield(g_L, -1, group);
    if(!lua_istable(g_L, -1)) { lua_pop(g_L, 1); lua_newtable(g_L); lua_pushvalue(g_L, -1); lua_setfield(g_L, -3, group); }
    luaL_setfuncs(g_L, funcs, 0); lua_settop(g_L, 0);
}
static void reparse(const std::string& s) { g_src = s; if(g_tree) ts_tree_delete(g_tree); g_tree = ts_parser_parse_string(g_parser, nullptr, g_src.data(), g_src.size()); ++g_gen; }
static int l_set_source(lua_State* L) { reparse(luaL_checkstring(L, 1)); return 0; }
static int l_bump(lua_State*) { ++g_gen; return 0; }   // an edit that left the text (and tree object) alone
static int l_src_len(lua_State* L) { lua_pushinteger(L, g_src.size()); return 1; }
int main(int, char** argv) {
    g_parser = ts_parser_new(); ts_parser_set_language(g_parser, tree_sitter_c());
    g_L = luaL_newstate(); luaL_openlibs(g_L); lua_newtable(g_L); lua_setglobal(g_L, "winter");
    lua_register(g_L, "set_source", l_set_source); lua_register(g_L, "bump", l_bump); lua_register(g_L, "src_len", l_src_len);
    TsProvider p;
    p.snapshot = [](uint64_t id, TsSnapshot& out) {
        if(id == 0 || id == 1) { out.buf = 1; out.tree = g_tree; out.gen = g_gen; out.lang = "c";
            out.text = [](size_t a, size_t b) { a = std::min(a, g_src.size()); b = std::min(b, g_src.size()); return a < b ? g_src.substr(a, b - a) : std::string(); }; return true; }
        if(id == 2) { out.buf = 2; return true; }                       // a buffer with no language
        if(id == 3) { out.buf = 3; out.lang = "zig"; return true; }     // a language without a parser
        return false;
    };
    p.language = [](std::string_view n) { return n == "c" ? tree_sitter_c() : nullptr; };
    ts_set_provider(std::move(p));
    script_open_ts();
    reparse("int add(int a, int b) { return a + b; }\nstatic void log_msg(const char *s) { puts(s); }\n");
    if(luaL_dofile(g_L, argv[1])) { std::printf("LUA ERROR: %s\n", lua_tostring(g_L, -1)); return 1; }
    lua_close(g_L); ts_tree_delete(g_tree); ts_parser_delete(g_parser);
}
