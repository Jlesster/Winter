// ---- decorations and floating windows ----------------------------------
//
// Positions: line and column are 1-based, the column a byte index, like
// winter.cursor(). Screen cells (floats) are 0-based. Nothing below keeps a
// C++ object with a destructor alive across a call that can raise a Lua error.

// the name <C-x> / <Esc> / <Tab> / ... in the notation winter.map() parses
std::string key_name(int c) {
    switch(c) {
        case '\x1b': return "<Esc>";
        case '\r': return "<CR>";
        case '\t': return "<Tab>";
        case ' ': return "<Space>";
        case '<': return "<lt>";
        case BACKSPACE: return "<BS>";
        case CTRL_BACKSPACE: return "<C-BS>";
        case ARROW_LEFT: return "<Left>";
        case ARROW_RIGHT: return "<Right>";
        case ARROW_UP: return "<Up>";
        case ARROW_DOWN: return "<Down>";
        case PAGE_UP: return "<PageUp>";
        case PAGE_DOWN: return "<PageDown>";
        case DELETE: return "<Del>";
        case HOME: return "<Home>";
        case END: return "<End>";
        default: break;
    }
    if(c >= 1 && c <= 26) return std::format("<C-{}>", static_cast<char>('a' + c - 1));
    if(c >= 0 && c < 256) return std::string(1, static_cast<char>(c));  // incl. UTF-8 bytes
    return std::format("<{}>", c);
}

// t = table index of opts, or 0 when absent. Missing keys keep the defaults.
int opt_int(lua_State* L, int t, const char* key, int dflt) {
    if(!t) return dflt;
    lua_getfield(L, t, key);
    const int v = lua_isinteger(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : dflt;
    lua_pop(L, 1);
    return v;
}

bool opt_bool(lua_State* L, int t, const char* key, bool dflt) {
    if(!t) return dflt;
    lua_getfield(L, t, key);
    const bool v = lua_isnil(L, -1) ? dflt : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}

// 0xRRGGBB or "#rrggbb"; anything else (or missing) leaves the colour alone.
// A malformed string sets *bad so the caller can raise after cleaning up.
uint32_t opt_color(lua_State* L, int t, const char* key, bool* bad) {
    uint32_t c = 0xFFFFFFFF;
    if(!t) return c;
    lua_getfield(L, t, key);
    if(lua_isinteger(L, -1)) {
        c = static_cast<uint32_t>(lua_tointeger(L, -1)) & 0xFFFFFF;
    } else if(lua_type(L, -1) == LUA_TSTRING) {
        const char* s = lua_tostring(L, -1);
        char* end = nullptr;
        const unsigned long v = s[0] == '#' && std::strlen(s) == 7 ? std::strtoul(s + 1, &end, 16) : 0;
        if(end && *end == '\0') c = static_cast<uint32_t>(v);
        else *bad = true;
    }
    lua_pop(L, 1);
    return c;
}

// {fg=, bg=, ul=, bold=, italic=, underline=, undercurl=, dim=, reverse=}
Style read_style(lua_State* L, int t, bool* bad) {
    Style st;
    st.fg = opt_color(L, t, "fg", bad);
    st.bg = opt_color(L, t, "bg", bad);
    st.ul = opt_color(L, t, "ul", bad);
    uint16_t a = 0;
    if(opt_bool(L, t, "bold", false)) a |= ATTR_BOLD;
    if(opt_bool(L, t, "dim", false)) a |= ATTR_DIM;
    if(opt_bool(L, t, "italic", false)) a |= ATTR_ITALIC;
    if(opt_bool(L, t, "underline", false)) a |= ATTR_UNDERLINE;
    if(opt_bool(L, t, "undercurl", false)) a |= ATTR_UNDERCURL;
    if(opt_bool(L, t, "reverse", false)) a |= ATTR_REVERSE;
    st.attrs = a;
    return st;
}

int table_arg(lua_State* L, int i) {  // optional table: its index, or 0
    if(lua_isnoneornil(L, i)) return 0;
    luaL_checktype(L, i, LUA_TTABLE);
    return lua_absindex(L, i);
}

// 1-based line and byte column to an offset, clamped into the line
size_t offset_of(lua_Integer line, lua_Integer col) {
    PieceTable& pt = E.buf().pt;
    const size_t l = static_cast<size_t>(line - 1);
    const size_t ls = pt.line_start(l);
    const size_t want = ls + static_cast<size_t>(std::max<lua_Integer>(col - 1, 0));
    return std::min(want, pt.line_end(l));
}

int l_ns(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 1, &n);
    lua_pushinteger(L, deco_ns(std::string_view(s, n)));
    return 1;
}

// winter.eol(ns, line, text [, style]): text after the end of a line. It stays
// with the line as text is edited. Styles: see read_style. `prio` orders items
// on one line (higher first, so leftmost); diagnostics sit at 99.
int l_eol(lua_State* L) {
    const lua_Integer ns = luaL_checkinteger(L, 1);
    const lua_Integer line = luaL_checkinteger(L, 2);
    size_t n;
    const char* text = luaL_checklstring(L, 3, &n);
    const int t = table_arg(L, 4);
    luaL_argcheck(L, line >= 1 && line <= E.num_rows(), 2, "line out of range");
    bool bad = false;
    const Style st = read_style(L, t, &bad);
    const int prio = opt_int(L, t, "prio", 0);
    if(bad) return luaL_argerror(L, 4, "colors are 0xRRGGBB or \"#rrggbb\"");
    E.buf().sync_anchors();  // offsets below are for the current text
    std::string_view first(text, n);  // EOL text never wraps: first line only
    first = first.substr(0, first.find('\n'));
    E.buf().decos.add({.pos = E.buf().pt.line_end(static_cast<size_t>(line - 1)),
                       .ns = static_cast<NsId>(ns),
                       .prio = prio,
                       .vt = {std::string(first), st}});
    return 0;
}

// winter.span(ns, line1, col1, line2, col2 [, style]): restyle [start, end),
// end exclusive. Both ends follow edits.
int l_span(lua_State* L) {
    const lua_Integer ns = luaL_checkinteger(L, 1);
    const lua_Integer l1 = luaL_checkinteger(L, 2), c1 = luaL_checkinteger(L, 3);
    const lua_Integer l2 = luaL_checkinteger(L, 4), c2 = luaL_checkinteger(L, 5);
    const int t = table_arg(L, 6);
    luaL_argcheck(L, l1 >= 1 && l1 <= E.num_rows(), 2, "line out of range");
    luaL_argcheck(L, l2 >= 1 && l2 <= E.num_rows(), 4, "line out of range");
    bool bad = false;
    const Style st = read_style(L, t, &bad);
    const int prio = opt_int(L, t, "prio", 0);
    if(bad) return luaL_argerror(L, 6, "colors are 0xRRGGBB or \"#rrggbb\"");
    E.buf().sync_anchors();
    const size_t a = offset_of(l1, c1), b = offset_of(l2, c2);
    luaL_argcheck(L, b >= a, 4, "range ends before it starts");
    if(b > a)
        E.buf().decos.add_span({.start = a,
                                .end = b,
                                .ns = static_cast<NsId>(ns),
                                .prio = prio,
                                .st = st});
    return 0;
}

int l_clear_ns(lua_State* L) {
    E.buf().decos.clear_ns(static_cast<NsId>(luaL_checkinteger(L, 1)));
    return 0;
}

// keeps the Lua callbacks of a float alive exactly as long as the float
struct FloatRefs {
    int on_key = LUA_NOREF;
    int on_close = LUA_NOREF;
    ~FloatRefs() {
        if(!g_L) return;  // Lua already shut down
        luaL_unref(g_L, LUA_REGISTRYINDEX, on_key);
        luaL_unref(g_L, LUA_REGISTRYINDEX, on_close);
    }
};

int text_width(std::string_view s) {
    int w = 0;
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        i += d.len;
        w += cell_width(d.cp, 0);
    }
    return w;
}

// winter.float{ at="cursor"|"screen", x=, y=, w=, h=, z=, cover_bars=,
//               dismiss_on_move=, esc_closes=, visible=,
//               lines={...}, style={...},
//               on_key=function(name, code) return consumed end,
//               on_close=function() end }  -> id
// `lines` (optional) fills the float and sizes it when w/h are missing.
int l_float(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    OverlayId id = 0;
    const char* err = nullptr;
    {
        OverlaySpec spec;
        bool bad = false;
        lua_getfield(L, 1, "at");
        if(lua_type(L, -1) == LUA_TSTRING) {
            const std::string_view at = lua_tostring(L, -1);
            if(at == "screen") spec.at = OverlaySpec::At::Screen;
            else if(at != "cursor") err = "float: at is \"cursor\" or \"screen\"";
        }
        lua_pop(L, 1);
        spec.x = opt_int(L, 1, "x", 0);
        spec.y = opt_int(L, 1, "y", spec.at == OverlaySpec::At::Cursor ? 1 : 0);
        spec.z = opt_int(L, 1, "z", 0);
        spec.cover_bars = opt_bool(L, 1, "cover_bars", false);
        spec.dismiss_on_move = opt_bool(L, 1, "dismiss_on_move", true);
        spec.esc_closes = opt_bool(L, 1, "esc_closes", true);
        spec.visible = opt_bool(L, 1, "visible", true);

        Style st;
        lua_getfield(L, 1, "style");
        if(lua_istable(L, -1)) st = read_style(L, lua_gettop(L), &bad);
        lua_pop(L, 1);
        if(bad) err = "float: colors are 0xRRGGBB or \"#rrggbb\"";

        std::vector<std::string> lines;
        lua_getfield(L, 1, "lines");
        if(lua_istable(L, -1)) {
            const lua_Integer count = luaL_len(L, -1);
            for(lua_Integer i = 1; i <= count; ++i) {
                lua_rawgeti(L, -1, i);
                size_t n = 0;
                const char* s = lua_tolstring(L, -1, &n);
                lines.emplace_back(s ? std::string(s, n) : std::string());
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        int widest = 0;
        for(const std::string& l : lines) widest = std::max(widest, text_width(l));
        spec.w = opt_int(L, 1, "w", lines.empty() ? 1 : widest + 2);
        spec.h = opt_int(L, 1, "h", lines.empty() ? 1 : static_cast<int>(lines.size()));

        auto refs = std::make_shared<FloatRefs>();
        lua_getfield(L, 1, "on_key");
        if(lua_isfunction(L, -1)) refs->on_key = ref_function(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "on_close");
        if(lua_isfunction(L, -1)) refs->on_close = ref_function(L, -1);
        lua_pop(L, 1);
        if(refs->on_key != LUA_NOREF)
            spec.on_key = [refs](int c) -> bool {
                if(!g_L) return false;
                lua_rawgeti(g_L, LUA_REGISTRYINDEX, refs->on_key);
                const std::string name = key_name(c);
                lua_pushlstring(g_L, name.data(), name.size());
                lua_pushinteger(g_L, c);
                if(!call(2, 1)) return false;  // an erroring handler passes the key on
                const bool consumed = lua_toboolean(g_L, -1) != 0;
                lua_pop(g_L, 1);
                return consumed;
            };
        if(refs->on_close != LUA_NOREF)
            spec.on_close = [refs] {
                if(!g_L) return;
                lua_rawgeti(g_L, LUA_REGISTRYINDEX, refs->on_close);
                call(0);
            };

        if(!err) {
            id = overlay_open(std::move(spec));
            overlay_clear(id, st);
            for(size_t i = 0; i < lines.size(); ++i) overlay_put(id, 1, static_cast<int>(i), lines[i], st);
        }
    }
    if(err) return luaL_error(L, "%s", err);
    lua_pushinteger(L, id);
    return 1;
}

// winter.float_put(id, x, y, text [, style]) -> end x
int l_float_put(lua_State* L) {
    const OverlayId id = static_cast<OverlayId>(luaL_checkinteger(L, 1));
    const int x = static_cast<int>(luaL_checkinteger(L, 2));
    const int y = static_cast<int>(luaL_checkinteger(L, 3));
    size_t n;
    const char* s = luaL_checklstring(L, 4, &n);
    const int t = table_arg(L, 5);
    bool bad = false;
    const Style st = read_style(L, t, &bad);
    if(bad) return luaL_argerror(L, 5, "colors are 0xRRGGBB or \"#rrggbb\"");
    lua_pushinteger(L, overlay_put(id, x, y, std::string_view(s, n), st));
    return 1;
}

// winter.float_clear(id [, style]): blank the float with a background
int l_float_clear(lua_State* L) {
    const OverlayId id = static_cast<OverlayId>(luaL_checkinteger(L, 1));
    const int t = table_arg(L, 2);
    bool bad = false;
    const Style st = read_style(L, t, &bad);
    if(bad) return luaL_argerror(L, 2, "colors are 0xRRGGBB or \"#rrggbb\"");
    overlay_clear(id, st);
    return 0;
}

int l_float_close(lua_State* L) {
    overlay_close(static_cast<OverlayId>(luaL_checkinteger(L, 1)));
    return 0;
}

// winter.replace(line1, col1, line2, col2, text): swap [start, end) for text
// and leave the cursor after it. One undo step outside insert mode.
int l_replace(lua_State* L) {
    const lua_Integer l1 = luaL_checkinteger(L, 1), c1 = luaL_checkinteger(L, 2);
    const lua_Integer l2 = luaL_checkinteger(L, 3), c2 = luaL_checkinteger(L, 4);
    size_t n;
    const char* text = luaL_checklstring(L, 5, &n);
    luaL_argcheck(L, l1 >= 1 && l1 <= E.num_rows(), 1, "line out of range");
    luaL_argcheck(L, l2 >= 1 && l2 <= E.num_rows(), 3, "line out of range");
    const size_t a = offset_of(l1, c1), b = offset_of(l2, c2);
    luaL_argcheck(L, b >= a, 3, "range ends before it starts");
    {
        std::unique_ptr<PieceTable::Group> group;
        if(E.mode != INSERT) group = std::make_unique<PieceTable::Group>(E.buf().pt);
        if(b > a) E.buf().pt.remove(a, b - a);
        if(n) E.buf().pt.insert(a, std::string_view(text, n));
        set_cursor_offset(a + n);
        if(E.mode != INSERT)
            set_cursor_offset(motion::clamp_normal(E.buf().pt, cursor_offset()));
        update_want();
    }
    return 0;
}
