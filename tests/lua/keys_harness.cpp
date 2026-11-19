#include <lua.hpp>
#include <cstdio>
#include <vector>
#include "input.hpp"
#include "script_internal.hpp"
static lua_State* g_L;
static std::vector<std::vector<int>> g_inj, g_fed;
static bool g_complete = false, g_refuse = false;
lua_State* script_state() { return g_L; }
bool editor_inject_keys(const std::vector<int>& k, bool c) { if(g_refuse) return false; g_inj.push_back(k); g_complete = c; return true; }
void editor_feed_keys(std::vector<int> k) { g_fed.push_back(std::move(k)); }
void script_register(const char* group, const luaL_Reg* funcs) {
    lua_getglobal(g_L, "winter");
    if(group) { lua_getfield(g_L, -1, group); if(!lua_istable(g_L,-1)) { lua_pop(g_L,1); lua_newtable(g_L); lua_pushvalue(g_L,-1); lua_setfield(g_L,-3,group);} }
    luaL_setfuncs(g_L, funcs, 0); lua_settop(g_L, 0);
}
static int l_last_inj(lua_State* L) { lua_newtable(L); if(g_inj.empty()) return 1; int i = 1; for(int k : g_inj.back()) { lua_pushinteger(L, k); lua_rawseti(L, -2, i++); } lua_pushboolean(L, g_complete); return 2; }
static int l_last_fed(lua_State* L) { lua_newtable(L); if(g_fed.empty()) return 1; int i = 1; for(int k : g_fed.back()) { lua_pushinteger(L, k); lua_rawseti(L, -2, i++); } return 1; }
static int l_counts(lua_State* L) { lua_pushinteger(L, g_inj.size()); lua_pushinteger(L, g_fed.size()); return 2; }
static int l_refuse(lua_State* L) { g_refuse = lua_toboolean(L, 1); return 0; }
int main(int, char** argv) {
    g_L = luaL_newstate(); luaL_openlibs(g_L); lua_newtable(g_L); lua_setglobal(g_L, "winter");
    script_open_keys();
    lua_register(g_L, "last_inj", l_last_inj); lua_register(g_L, "last_fed", l_last_fed); lua_register(g_L, "counts", l_counts); lua_register(g_L, "refuse", l_refuse);
    if(luaL_dofile(g_L, argv[1])) { std::printf("LUA ERROR: %s\n", lua_tostring(g_L, -1)); return 1; }
}
