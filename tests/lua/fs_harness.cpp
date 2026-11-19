#include <lua.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "script.hpp"
#include "script_internal.hpp"
static lua_State* g_L;
lua_State* script_state() { return g_L; }
std::string script_config_dir() { return std::string(std::getenv("WT")) + "/cfg"; }
void script_register(const char* group, const luaL_Reg* funcs) {
    lua_getglobal(g_L, "winter");
    if(group) {
        lua_getfield(g_L, -1, group);
        if(!lua_istable(g_L, -1)) { lua_pop(g_L, 1); lua_newtable(g_L); lua_pushvalue(g_L, -1); lua_setfield(g_L, -3, group); }
    }
    luaL_setfuncs(g_L, funcs, 0);
    lua_settop(g_L, 0);
}
int main(int argc, char** argv) {
    g_L = luaL_newstate(); luaL_openlibs(g_L);
    lua_newtable(g_L); lua_setglobal(g_L, "winter");
    script_open_fs();
    if(luaL_dofile(g_L, argv[1])) { std::printf("LUA ERROR: %s\n", lua_tostring(g_L, -1)); return 1; }
    return 0;
}
