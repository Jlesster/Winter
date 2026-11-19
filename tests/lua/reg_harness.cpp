#include <lua.hpp>
#include <cstdio>
#include "script_internal.hpp"
#include "shell.hpp"
static lua_State* g_L; static std::string g_clip;
lua_State* script_state() { return g_L; }
ShellResult shell_run(const std::string& cmd, const std::string* in) {
    ShellResult r;
    if(cmd.find("wl-copy") != std::string::npos) g_clip = in ? *in : "";
    else if(cmd.find("wl-paste") != std::string::npos) { r.out = g_clip; r.code = g_clip.empty(); }
    return r;
}
void script_register(const char* group, const luaL_Reg* funcs) {
    lua_getglobal(g_L, "winter"); lua_getfield(g_L, -1, group);
    if(!lua_istable(g_L, -1)) { lua_pop(g_L, 1); lua_newtable(g_L); lua_pushvalue(g_L, -1); lua_setfield(g_L, -3, group); }
    luaL_setfuncs(g_L, funcs, 0); lua_settop(g_L, 0);
}
int main(int, char** argv) {
    g_L = luaL_newstate(); luaL_openlibs(g_L); lua_newtable(g_L); lua_setglobal(g_L, "winter");
    script_open_reg();
    if(luaL_dofile(g_L, argv[1])) { std::printf("LUA ERROR: %s\n", lua_tostring(g_L, -1)); return 1; }
}
