// winter.mark: marks for plugins (see marks.hpp). Lines and columns are
// 1-based here, like the rest of winter.*; buf is a stable buffer id, 0 = current.
#include <algorithm>
#include <lua.hpp>
#include <string>

#include "editor.hpp"
#include "jumplist.hpp"
#include "marks.hpp"
#include "script_internal.hpp"

namespace {

uint64_t buf_arg(lua_State* L, int i) {
    const lua_Integer id = luaL_optinteger(L, i, 0);
    return id == 0 ? E.buf().id : static_cast<uint64_t>(id);
}

char name_arg(lua_State* L, int i) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, i, &n);
    luaL_argcheck(L, n == 1, i, "a one-character mark name");
    return s[0];
}

void push_pos(lua_State* L, char name, const MarkPos& m) {
    lua_createtable(L, 0, 5);
    lua_pushlstring(L, &name, 1);
    lua_setfield(L, -2, "name");
    lua_pushinteger(L, static_cast<lua_Integer>(m.buf));  // 0: its buffer is closed
    lua_setfield(L, -2, "buf");
    lua_pushlstring(L, m.file.data(), m.file.size());
    lua_setfield(L, -2, "file");
    lua_pushinteger(L, static_cast<lua_Integer>(m.line) + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, static_cast<lua_Integer>(m.col) + 1);
    lua_setfield(L, -2, "col");
}

// winter.mark.set(name [, {buf=, line=, col=}]) -> bool
// Without a position: the cursor of the current buffer. a-z live in `buf`;
// A-Z are file marks; ' is the jump-origin mark. false for a bad name.
int l_set(lua_State* L) {
    const char name = name_arg(L, 1);
    bool ok;
    if(lua_istable(L, 2)) {
        lua_getfield(L, 2, "buf");
        lua_getfield(L, 2, "line");
        lua_getfield(L, 2, "col");
        const lua_Integer id = luaL_optinteger(L, -3, 0);
        const lua_Integer line = luaL_optinteger(L, -2, 1);
        const lua_Integer col = luaL_optinteger(L, -1, 1);
        lua_pop(L, 3);
        ok = mark_put_at(name, id == 0 ? E.buf().id : static_cast<uint64_t>(id),
                         static_cast<size_t>(std::max<lua_Integer>(line, 1) - 1),
                         static_cast<size_t>(std::max<lua_Integer>(col, 1) - 1));
    } else {
        ok = mark_put(name, E.buf().id, cursor_offset());
    }
    lua_pushboolean(L, ok);
    return 1;
}

// winter.mark.get(name [, buf]) -> {name, buf, file, line, col} | nil
// buf = 0 means the file is closed: open `file` to use it.
int l_get(lua_State* L) {
    const char name = name_arg(L, 1);
    const auto m = mark_find(name, buf_arg(L, 2));
    if(!m) {
        lua_pushnil(L);
        return 1;
    }
    push_pos(L, name, *m);
    return 1;
}

int l_del(lua_State* L) {
    const char name = name_arg(L, 1);
    lua_pushboolean(L, mark_del(name, buf_arg(L, 2)));
    return 1;
}

// winter.mark.list([buf]) -> { {name, buf, file, line, col}, ... }
// that buffer's marks (a-z . < >) followed by the file marks and '
int l_list(lua_State* L) {
    const uint64_t buf = buf_arg(L, 1);
    lua_newtable(L);
    lua_Integer i = 1;
    for(const char c : mark_names(buf))
        if(const auto m = mark_find(c, buf)) {
            push_pos(L, c, *m);
            lua_rawseti(L, -2, i++);
        }
    return 1;
}

// winter.mark.jump(name [, {exact=false}]) -> bool: like '{name}, or `{name}
int l_jump(lua_State* L) {
    const char name = name_arg(L, 1);
    bool exact = false;
    if(lua_istable(L, 2)) {
        lua_getfield(L, 2, "exact");
        exact = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    lua_pushboolean(L, jump_to_mark(name, exact));
    return 1;
}

const luaL_Reg kMark[] = {{"set", l_set},
                          {"get", l_get},
                          {"del", l_del},
                          {"list", l_list},
                          {"jump", l_jump},
                          {nullptr, nullptr}};

}  // namespace

void script_open_mark() { script_register("mark", kMark); }
