// winter.json: encode/decode for plugins, on the same Lua <-> Json conversion
// the config loader uses.
#include <lua.hpp>
#include <string>

#include "json.hpp"
#include "lua_json.hpp"
#include "script_internal.hpp"

namespace {

// winter.json.encode(value) -> string | nil, err
// Tables with 1..n keys become arrays, others objects; functions and userdata
// are errors.
int l_encode(lua_State* L) {
    luaL_checkany(L, 1);
    std::string out, err;
    bool ok;
    {
        Json j;
        ok = lua_to_json(L, 1, j, err);
        if(ok) out = j.dump();
    }
    if(!ok) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// winter.json.decode(text) -> value | nil, err. JSON null becomes nil, so an
// array holding nulls has holes.
int l_decode(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    std::string err;
    bool ok;
    {
        const auto j = Json::parse(std::string_view(s, n), &err);
        ok = j.has_value();
        if(ok) lua_push_json(L, *j);
    }
    if(!ok) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    return 1;
}

const luaL_Reg kJson[] = {
    {"encode", l_encode}, {"decode", l_decode}, {nullptr, nullptr}};

}  // namespace

void script_open_json() { script_register("json", kJson); }
