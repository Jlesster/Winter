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

#include "lua_json.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <lua.hpp>
#include <string_view>
#include <vector>

namespace {
constexpr int kMaxDepth = 64;

struct Conv {
    lua_State* L;
    std::string err{};
    std::vector<std::string> path{};

    bool fail(std::string msg) {
        std::string p;
        for(const std::string& s : path) p += s;
        if(!p.empty() && p.front() == '.') p.erase(0, 1);
        err = p.empty() ? std::move(msg) : std::format("{} at {}", msg, p);
        return false;
    }

    bool value(int idx, Json& out, int depth) {
        switch(lua_type(L, idx)) {
            case LUA_TNIL: out = Json(); return true;
            case LUA_TBOOLEAN:
                out = Json(lua_toboolean(L, idx) != 0);
                return true;
            case LUA_TNUMBER:
                if(lua_isinteger(L, idx))
                    out = Json(static_cast<int64_t>(lua_tointeger(L, idx)));
                else out = Json(static_cast<double>(lua_tonumber(L, idx)));
                return true;
            case LUA_TSTRING: {
                size_t n = 0;
                const char* s = lua_tolstring(L, idx, &n);
                out = Json(std::string_view(s, n));
                return true;
            }
            case LUA_TTABLE: return table(idx, out, depth);
            default:
                return fail(std::format("unsupported type '{}'",
                                        luaL_typename(L, idx)));
        }
    }

    bool table(int idx, Json& out, int depth) {
        if(depth >= kMaxDepth) return fail("nested too deep (cycle?)");
        if(!lua_checkstack(L, 8)) return fail("out of Lua stack");
        idx = lua_absindex(L, idx);
        size_t count = 0;
        lua_Integer lo = 0, hi = 0;
        bool has_str = false, has_int = false;
        lua_pushnil(L);
        while(lua_next(L, idx)) {
            lua_pop(L, 1);
            ++count;
            if(lua_type(L, -1) == LUA_TSTRING) {
                has_str = true;
            } else if(lua_isinteger(L, -1)) {
                const lua_Integer k = lua_tointeger(L, -1);
                lo = has_int ? std::min(lo, k) : k;
                hi = has_int ? std::max(hi, k) : k;
                has_int = true;
            } else {
                return fail("table keys must be strings or array indices");
            }
        }
        if(has_str && has_int) return fail("table mixes list and map entries");
        if(has_int) {
            if(lo < 1 || static_cast<size_t>(hi) != count)
                return fail("list has holes (indices must be exactly 1..n)");
            Json::Array a;
            a.reserve(count);
            for(lua_Integer i = 1; i <= hi; ++i) {
                lua_rawgeti(L, idx, i);
                path.push_back(std::format("[{}]", i));
                Json v;
                const bool ok = value(-1, v, depth + 1);
                path.pop_back();
                if(!ok) return false;
                lua_pop(L, 1);
                a.push_back(std::move(v));
            }
            out = Json::from_array(std::move(a));
            return true;
        }
        Json::Object o;
        o.reserve(count);
        lua_pushnil(L);
        while(lua_next(L, idx)) {
            size_t n = 0;
            const char* k = lua_tolstring(L, -2, &n);
            std::string key(k, n);
            path.push_back("." + key);
            Json v;
            const bool ok = value(-1, v, depth + 1);
            path.pop_back();
            if(!ok) return false;
            o.emplace_back(std::move(key), std::move(v));
            lua_pop(L, 1);
        }
        out = Json::from_object(std::move(o));
        return true;
    }
};

}  // namespace

void lua_push_json(lua_State* L, const Json& j) {
    luaL_checkstack(L, 4, "json nested too deeply");
    if(j.is_null()) lua_pushnil(L);
    else if(j.is_bool()) lua_pushboolean(L, j.as_bool());
    else if(j.is_int())
        lua_pushinteger(L, static_cast<lua_Integer>(j.as_int()));
    else if(j.is_number()) lua_pushnumber(L, j.as_double());
    else if(j.is_string()) {
        const std::string_view s = j.as_str();
        lua_pushlstring(L, s.data(), s.size());
    } else if(j.is_array()) {
        lua_createtable(L, static_cast<int>(j.size()), 0);
        lua_Integer i = 1;
        for(const Json& v : j.items()) {
            lua_push_json(L, v);
            lua_rawseti(L, -2, i++);
        }
    } else {
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for(const auto& [k, v] : j.members()) {
            lua_push_json(L, v);
            lua_setfield(L, -2, k.c_str());
        }
    }
}

bool lua_to_json(lua_State* L, int idx, Json& out, std::string& err) {
    const int top = lua_gettop(L);
    idx = lua_absindex(L, idx);
    Conv c{L};
    const bool ok = c.value(idx, out, 0);
    lua_settop(L, top);
    if(!ok) err = std::move(c.err);
    return ok;
}

std::optional<Json> lua_load_config_file(const std::string& path,
                                         std::string& err) {
    lua_State* L = luaL_newstate();
    if(!L) {
        err = "out of memory";
        return std::nullopt;
    }
    static const luaL_Reg libs[] = {{"_G", luaopen_base},
                                    {LUA_STRLIBNAME, luaopen_string},
                                    {LUA_TABLIBNAME, luaopen_table},
                                    {LUA_MATHLIBNAME, luaopen_math},
                                    {nullptr, nullptr}};
    for(const luaL_Reg* l = libs; l->func; ++l) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    for(const char* g : {"dofile", "loadfile"}) {
        lua_pushnil(L);
        lua_setglobal(L, g);
    }
    luaL_dostring(L,
                  "vim = setmetatable({}, {__index = function(t) return t end, "
                  "__call = function(t) return t end})");
    if(luaL_loadfile(L, path.c_str()) != LUA_OK ||
       lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char* m = lua_tostring(L, -1);
        err = m ? m : "failed to load";
        lua_close(L);
        return std::nullopt;
    }
    if(!lua_istable(L, -1)) {
        err = "file did not return a table";
        lua_close(L);
        return std::nullopt;
    }
    const int res = lua_gettop(L);
    lua_createtable(L, 0, 4);
    for(const char* k : {"cmd", "filetypes", "settings", "init_options"}) {
        lua_getfield(L, res, k);
        if(lua_isnil(L, -1) || lua_isfunction(L, -1)) lua_pop(L, 1);
        else lua_setfield(L, -2, k);
    }
    Json out;
    const bool ok = lua_to_json(L, -1, out, err);
    lua_close(L);
    if(!ok) return std::nullopt;
    return out;
}
