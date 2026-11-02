#pragma once
#include <lua.hpp>
#include <memory>
#include "registry.hpp"

lua_State* script_state();
bool script_call(int nargs, int nresults = 0);
void script_register(const char* group, const luaL_Reg* funcs);

class LuaRef {
public:
    LuaRef(lua_State* L, int idx) {
        lua_pushvalue(L, idx);
        ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    ~LuaRef() {
        if(lua_State* L = script_state()) luaL_unref(L, LUA_REGISTRYINDEX, ref_);
    }
    LuaRef(const LuaRef&) = delete;
    LuaRef& operator=(const LuaRef&) = delete;
    void push() const { lua_rawgeti(script_state(), LUA_REGISTRYINDEX, ref_); }

private:
    int ref_ = LUA_NOREF;
};

using LuaFn = std::shared_ptr<LuaRef>;
inline LuaFn lua_fn(lua_State* L, int idx) {
    return std::make_shared<LuaRef>(L, idx);
}

inline int lua_table_arg(lua_State* L, int i) {
    if(lua_isnoneornil(L, i)) return 0;
    luaL_checktype(L, i, LUA_TTABLE);
    return lua_absindex(L, i);
}

inline bool lua_opt_bool(lua_State* L, int t, const char* key, bool dflt) {
    if(!t) return dflt;
    lua_getfield(L, t, key);
    const bool v = lua_isnil(L, -1) ? dflt : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}

void script_push_range(lua_State* L, const Range& r);
void script_open_buf();
void script_open_ext();
