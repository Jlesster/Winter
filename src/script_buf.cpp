#include <algorithm>
#include <cstddef>
#include <lua.hpp>
#include <memory>
#include <string>
#include <string_view>

#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "piece.hpp"
#include "script_internal.hpp"
#include "syntax.hpp"
#include "utf8.hpp"

namespace {
PieceTable& pt() { return E.buf().pt; }

size_t offset_of(lua_Integer line, lua_Integer col) {
    const size_t l = static_cast<size_t>(line - 1);
    const size_t ls = pt().line_start(l);
    const size_t want =
        ls + static_cast<size_t>(std::max<lua_Integer>(col - 1, 0));
    return std::min(want, pt().line_end(l));
}

void settle_cursor() {
    E.cy = std::clamp(E.cy, 0, E.num_rows() - 1);
    if(!insertish(E.mode))
        set_cursor_offset(motion::clamp_normal(pt(), cursor_offset()));
    update_want();
}

int l_cursor_get(lua_State* L) {
    lua_pushinteger(L, E.cy + 1);
    lua_pushinteger(L, E.cx + 1);
    return 2;
}

int l_cursor_set(lua_State* L) {
    const lua_Integer line = luaL_checkinteger(L, 1);
    const lua_Integer col = luaL_optinteger(L, 2, 1);
    E.cy = static_cast<int>(
        std::clamp<lua_Integer>(line - 1, 0, E.num_rows() - 1));
    const std::string text = pt().line_text(E.cy);
    const int size = static_cast<int>(text.size());
    int cx = snap_cx(
        text, static_cast<int>(std::clamp<lua_Integer>(col - 1, 0, size)));
    if(!insertish(E.mode) && cx >= size && size > 0)
        cx = static_cast<int>(utf8_prev(text, static_cast<size_t>(size)));
    E.cx = cx;
    update_want();
    return 0;
}

int l_line(lua_State* L) {
    const lua_Integer n = luaL_optinteger(L, 1, E.cy + 1);
    if(n < 1 || n > E.num_rows()) {
        lua_pushnil(L);
        return 1;
    }
    const std::string text = pt().line_text(static_cast<size_t>(n - 1));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int l_line_count(lua_State* L) {
    lua_pushinteger(L, E.num_rows());
    return 1;
}

int l_lines(lua_State* L) {
    const lua_Integer n = E.num_rows();
    const lua_Integer a = std::max<lua_Integer>(luaL_optinteger(L, 1, 1), 1);
    const lua_Integer b = std::min(luaL_optinteger(L, 2, n), n);
    lua_newtable(L);
    for(lua_Integer l = a; l <= b; ++l) {
        const std::string text = pt().line_text(static_cast<size_t>(l - 1));
        lua_pushlstring(L, text.data(), text.size());
        lua_rawseti(L, -2, l - a + 1);
    }
    return 1;
}

void apply_set_lines(lua_Integer first, lua_Integer last, lua_Integer cnt,
                     const std::string& text) {
    PieceTable& t = pt();
    const lua_Integer n = E.num_rows();
    std::unique_ptr<PieceTable::Group> group;
    if(!insertish(E.mode)) group = std::make_unique<PieceTable::Group>(t);
    const size_t f = static_cast<size_t>(first - 1);
    if(last < first) {  // nothing replaced: insert before line `first`
        if(cnt == 0) return;
        if(first <= n) t.insert(t.line_start(f), text + "\n");
        else t.insert(t.size(), "\n" + text);
    } else {
        const size_t lo = t.line_start(f);
        const size_t hi = t.line_end(static_cast<size_t>(last - 1));
        if(cnt > 0) {
            if(hi > lo) t.remove(lo, hi - lo);
            t.insert(lo, text);
        } else if(hi < t.size()) {  // delete the lines, take the \n after
            t.remove(lo, hi + 1 - lo);
        } else if(lo > 0) {  // file tail: take the \n before
            t.remove(lo - 1, hi - (lo - 1));
        } else {
            t.remove(lo, hi - lo);
        }
    }
    settle_cursor();
}

// winter.buf.set_lines(first, last, {string...}): replace lines first..last
// (inclusive) with the list. last == first - 1 inserts before `first`, an
// empty list deletes. One undo step.
int l_set_lines(lua_State* L) {
    const lua_Integer n = E.num_rows();
    const lua_Integer first = luaL_checkinteger(L, 1);
    const lua_Integer last = luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_argcheck(L, first >= 1 && first <= n + 1, 1, "line out of range");
    luaL_argcheck(L, last >= first - 1 && last <= n, 2, "line out of range");
    bool bad = false;
    {
        std::string text;
        const lua_Integer cnt = static_cast<lua_Integer>(lua_rawlen(L, 3));
        for(lua_Integer i = 1; i <= cnt && !bad; ++i) {
            lua_rawgeti(L, 3, i);
            if(lua_type(L, -1) != LUA_TSTRING) {
                bad = true;
            } else {
                size_t len;
                const char* s = lua_tolstring(L, -1, &len);
                if(i > 1) text += '\n';
                text.append(s, len);
            }
            lua_pop(L, 1);
        }
        if(!bad) apply_set_lines(first, last, cnt, text);
    }
    if(bad) return luaL_argerror(L, 3, "expected a list of strings");
    return 0;
}

// winter.buf.text(line1, col1, line2, col2) -> string   [start, end)
int l_text(lua_State* L) {
    const lua_Integer l1 = luaL_checkinteger(L, 1),
                      c1 = luaL_checkinteger(L, 2);
    const lua_Integer l2 = luaL_checkinteger(L, 3),
                      c2 = luaL_checkinteger(L, 4);
    luaL_argcheck(L, l1 >= 1 && l1 <= E.num_rows(), 1, "line out of range");
    luaL_argcheck(L, l2 >= 1 && l2 <= E.num_rows(), 3, "line out of range");
    const size_t a = offset_of(l1, c1), b = offset_of(l2, c2);
    luaL_argcheck(L, b >= a, 3, "range ends before it starts");
    const std::string s = pt().text(a, b - a);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// winter.buf.insert(text): at the cursor, cursor ends after it
int l_insert(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    const std::string_view text(s, n);
    std::unique_ptr<PieceTable::Group> group;
    if(!insertish(E.mode)) group = std::make_unique<PieceTable::Group>(pt());
    pt().insert(cursor_offset(), text);
    const auto newlines = std::count(text.begin(), text.end(), '\n');
    if(newlines == 0) {
        E.cx += static_cast<int>(text.size());
    } else {
        E.cy += static_cast<int>(newlines);
        E.cx = static_cast<int>(text.size() - (text.rfind('\n') + 1));
    }
    if(!insertish(E.mode))
        set_cursor_offset(motion::clamp_normal(pt(), cursor_offset()));
    update_want();
    return 0;
}

int l_name(lua_State* L) {
    lua_pushlstring(L, E.buf().filename.data(), E.buf().filename.size());
    return 1;
}

// the tree-sitter language name, "" when there is none
int l_lang(lua_State* L) {
    const std::string_view l = E.buf().syntax.lang();
    lua_pushlstring(L, l.data(), l.size());
    return 1;
}

int l_mode(lua_State* L) {
    lua_pushstring(L, mode_name(E.mode));
    return 1;
}

// winter.selection() -> {line1,col1,line2,col2,linewise} | nil outside visual
int l_selection(lua_State* L) {
    if(E.mode != VISUAL) {
        lua_pushnil(L);
        return 1;
    }
    script_push_range(L, visual_range());
    return 1;
}

const luaL_Reg kBuf[] = {{"line", l_line},
                         {"line_count", l_line_count},
                         {"lines", l_lines},
                         {"set_lines", l_set_lines},
                         {"text", l_text},
                         {"insert", l_insert},
                         {"name", l_name},
                         {"lang", l_lang},
                         {nullptr, nullptr}};
const luaL_Reg kCursor[] = {
    {"get", l_cursor_get}, {"set", l_cursor_set}, {nullptr, nullptr}};
const luaL_Reg kTop[] = {
    {"mode", l_mode}, {"selection", l_selection}, {nullptr, nullptr}};

}  // namespace

void script_push_range(lua_State* L, const Range& r) {
    const PieceTable& t = pt();
    const size_t last = r.end > r.start ? r.end - 1 : r.start;
    const size_t a = t.line_of(r.start), b = t.line_of(last);
    lua_createtable(L, 0, 5);
    const auto field = [L](const char* k, size_t v) {
        lua_pushinteger(L, static_cast<lua_Integer>(v));
        lua_setfield(L, -2, k);
    };
    field("line1", a + 1);
    field("col1", r.start - t.line_start(a) + 1);
    field("line2", b + 1);
    field("col2", r.end - t.line_start(b) + 1);
    lua_pushboolean(L, r.linewise);
    lua_setfield(L, -2, "linewise");
}

void script_open_buf() {
    script_register("buf", kBuf);
    script_register("cursor", kCursor);
    script_register(nullptr, kTop);
}
