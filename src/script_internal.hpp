/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <lua.hpp>
#include <memory>
#include "registry.hpp"

lua_State* script_state();
bool script_call(int nargs, int nresults = 0);
void script_register(const char* group, const luaL_Reg* funcs);
void script_ext_reset();

class LuaRef {
public:
    LuaRef(lua_State* L, int idx) {
        lua_pushvalue(L, idx);
        ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    ~LuaRef() {
        if(lua_State* L = script_state())
            luaL_unref(L, LUA_REGISTRYINDEX, ref_);
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
void script_open_fs();
void script_open_api();
void script_open_json();
void script_open_reg();
void script_open_mark();
void script_open_keys();
void script_open_win();
void script_open_ts();
void ts_install_provider();  // the editor-backed source for winter.ts
void script_api_reset();  // config reload: drop buffer attachments
void script_open_runtime();   // runtime.lua, after every native group exists
void script_runtime_reset();  // config reload
