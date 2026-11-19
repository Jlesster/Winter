// winter.reg: the editor's registers (reg.hpp) for plugins.
#include <lua.hpp>
#include <optional>
#include <string>
#include <vector>

#include "reg.hpp"
#include "script_internal.hpp"

namespace {

// winter.reg.get([name='"']) -> text, linewise | nil (empty or unknown)
// name is one character: " 0-9 - a-z + * %  (see reg.hpp)
int l_reg_get(lua_State* L) {
    size_t n = 0;
    const char* nm = luaL_optlstring(L, 1, "\"", &n);
    luaL_argcheck(L, n == 1, 1, "a one-character register name");
    const std::optional<RegValue> v = reg_get(nm[0]);
    if(!v) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, v->text.data(), v->text.size());
    lua_pushboolean(L, v->linewise);
    return 2;
}

// winter.reg.set(name, text [, linewise=false]) -> bool
// An uppercase name appends. Setting '"', '+' or '*' also updates the system
// clipboard. false for a read-only or unknown register.
int l_reg_set(lua_State* L) {
    size_t n = 0, tn = 0;
    const char* nm = luaL_checklstring(L, 1, &n);
    luaL_argcheck(L, n == 1, 1, "a one-character register name");
    const char* t = luaL_checklstring(L, 2, &tn);
    const bool linewise = lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, reg_put(nm[0], std::string(t, tn), linewise));
    return 1;
}

// winter.reg.list() -> { "\"", "0", "a", ... }: the registers holding something
int l_reg_list(lua_State* L) {
    const std::vector<char> names = reg_list();
    lua_createtable(L, static_cast<int>(names.size()), 0);
    lua_Integer i = 1;
    for(const char c : names) {
        lua_pushlstring(L, &c, 1);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

const luaL_Reg kReg[] = {{"get", l_reg_get},
                         {"set", l_reg_set},
                         {"list", l_reg_list},
                         {nullptr, nullptr}};

}  // namespace

void script_open_reg() { script_register("reg", kReg); }
