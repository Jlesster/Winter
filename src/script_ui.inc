// Included by script.cpp inside its anonymous namespace (after the theme
// bindings): winter.statusline / buf / win / job / timer / picker / ui.select /
// diag / options / lsp extras. Uses read_style, table_arg and friends from
// script_deco.inc.
//
// Rule kept from the rest of this file: a luaL_error longjmps, so no C++
// object with a destructor may be alive when one is raised.

void set_str(lua_State* L, const char* k, std::string_view v) {
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, -2, k);
}
void set_int(lua_State* L, const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}
void set_bool(lua_State* L, const char* k, bool v) {
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
}

// ---- winter.statusline -----------------------------------------------------

struct LuaComp {
    LuaFn fn;
    std::string name;
    Style dflt;
    int prio = 50;
    bool broken = false;  // threw once: hidden until it is redefined
};

constexpr Style kChip{.fg = 0xcdd6f4, .bg = 0x313244};

// What a component function receives.
void push_status_ctx(lua_State* L) {
    const Buffer& b = E.buf();
    lua_createtable(L, 0, 16);
    set_str(L, "mode", mode_name(E.mode));
    set_int(L, "width", E.term_cols);
    set_int(L, "line", E.cy + 1);
    set_int(L, "col", E.cx + 1);   // byte column, like winter.cursor
    set_int(L, "vcol", E.rx + 1);  // display column
    set_int(L, "lines", E.num_rows());
    set_str(L, "name", b.filename);
    set_str(L, "lang", b.syntax.lang());
    set_bool(L, "modified", b.pt.modified());
    set_int(L, "bufnr", static_cast<lua_Integer>(E.cur) + 1);
    set_int(L, "bufs", static_cast<lua_Integer>(E.buffers.size()));
    set_bool(L, "icons", opts.nerdicons);
    int n[5] = {};
    for(const Diag& d : b.diags)
        if(d.severity >= 1 && d.severity <= 4) ++n[d.severity];
    lua_createtable(L, 0, 4);
    set_int(L, "error", n[1]);
    set_int(L, "warn", n[2]);
    set_int(L, "info", n[3]);
    set_int(L, "hint", n[4]);
    lua_setfield(L, -2, "diag");
}

// one piece: "text" or { "text" | text=, fg=, bg=, bold=, ... }
void read_piece(lua_State* L, int idx, StatusBlock& b) {
    if(lua_type(L, idx) == LUA_TSTRING) {
        b.pieces.push_back({lua_tostring(L, idx), {}});
        return;
    }
    if(!lua_istable(L, idx)) return;
    lua_getfield(L, idx, "text");
    if(lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        lua_rawgeti(L, idx, 1);
    }
    if(lua_type(L, -1) == LUA_TSTRING) {
        bool bad = false;
        const Style st = read_style(L, idx, &bad);
        b.pieces.push_back({lua_tostring(L, -1), st});
    }
    lua_pop(L, 1);
}

// nil/false/"" hide the component. A string is one piece; a table is a block:
//   { "a", {"b", fg="#f38ba8"}, fg=, bg=, bold=, pad=false, priority=60 }
//   { text="a", fg=... }
bool read_block(lua_State* L, int idx, StatusBlock& b) {
    idx = lua_absindex(L, idx);
    if(lua_type(L, idx) == LUA_TSTRING) {
        if(lua_rawlen(L, idx) > 0) read_piece(L, idx, b);
        return !b.pieces.empty();
    }
    if(!lua_istable(L, idx)) return false;
    bool bad = false;
    style_patch(b.base, read_style(L, idx, &bad));
    b.pad = lua_opt_bool(L, idx, "pad", true);
    lua_getfield(L, idx, "priority");
    if(lua_isinteger(L, -1)) b.prio = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, idx, "text");
    if(lua_type(L, -1) == LUA_TSTRING && lua_rawlen(L, -1) > 0)
        b.pieces.push_back({lua_tostring(L, -1), {}});
    lua_pop(L, 1);
    const lua_Unsigned n = lua_rawlen(L, idx);
    for(lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
        read_piece(L, lua_gettop(L), b);
        lua_pop(L, 1);
    }
    return !b.pieces.empty();
}

StatusBlock run_lua_comp(const std::shared_ptr<LuaComp>& c) {
    StatusBlock b;
    b.prio = c->prio;
    b.base = theme_ui("ui.statusline." + c->name, c->dflt);
    lua_State* L = script_state();
    if(!L || c->broken) return StatusBlock{};
    const int top = lua_gettop(L);
    c->fn->push();
    push_status_ctx(L);
    if(!script_call(1, 1)) {  // reported once; stays hidden
        c->broken = true;
        lua_settop(L, top);
        return StatusBlock{};
    }
    if(!read_block(L, top + 1, b)) b.pieces.clear();
    lua_settop(L, top);
    return b;
}

void add_lua_component(const std::string& name, LuaFn fn, int prio, Style dflt) {
    auto c = std::make_shared<LuaComp>(
        LuaComp{std::move(fn), name, dflt, prio, false});
    statusline_add(name, [c] { return run_lua_comp(c); });
}

void read_comp_opts(lua_State* L, int t, int& prio, Style& dflt) {
    if(!t) return;
    lua_getfield(L, t, "priority");
    if(lua_isinteger(L, -1)) prio = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, t, "style");
    if(lua_istable(L, -1)) {
        bool bad = false;
        style_patch(dflt, read_style(L, lua_gettop(L), &bad));
    }
    lua_pop(L, 1);
}

// winter.statusline.component(name, fn(ctx) [, {priority=, style={...}}])
// Replaces a built-in of the same name. Style/colours can also come from the
// theme group ui.statusline.<name>.
int l_sl_component(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const int t = table_arg(L, 3);
    int prio = 50;
    Style dflt = kChip;
    read_comp_opts(L, t, prio, dflt);
    add_lua_component(name, lua_fn(L, 2), prio, dflt);
    return 0;
}

std::vector<std::string> g_sl_anon[2];  // functions placed straight in a layout
int g_sl_next = 1;

// layout side -> names; functions become anonymous components
bool read_sl_list(lua_State* L, int idx, int side, std::vector<std::string>& out) {
    for(const std::string& n : g_sl_anon[side]) statusline_remove(n);
    g_sl_anon[side].clear();
    const lua_Unsigned n = lua_rawlen(L, idx);
    for(lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
        const int ty = lua_type(L, -1);
        if(ty == LUA_TSTRING) {
            out.emplace_back(lua_tostring(L, -1));
        } else if(ty == LUA_TFUNCTION) {
            std::string name = std::format("lua.sl{}", g_sl_next++);
            add_lua_component(name, lua_fn(L, -1), 50, kChip);
            g_sl_anon[side].push_back(name);
            out.push_back(std::move(name));
        } else {
            lua_pop(L, 1);
            return false;
        }
        lua_pop(L, 1);
    }
    return true;
}

bool sep_preset(std::string_view p, std::optional<std::string>& l,
                std::optional<std::string>& r) {
    if(p == "auto") l.reset(), r.reset();
    else if(p == "none") l = "", r = "";
    else if(p == "powerline") l = "\ue0b0", r = "\ue0b2";
    else if(p == "round") l = "\ue0b4", r = "\ue0b6";
    else if(p == "slant") l = "\ue0bc", r = "\ue0be";
    else return false;
    return true;
}

// winter.statusline.setup({
//   left = {"mode", "file", fn, ...}, right = {...},     -- names or functions
//   separators = "powerline"|"round"|"slant"|"none"|"auto"|false
//                | { left = "\u{e0b0}", right = "\u{e0b2}" },
//   enabled = true,
// })   -- only the keys you pass change
int l_sl_setup(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* err = nullptr;
    {
        StatusLayout& lay = statusline_layout();
        std::vector<std::string> names;
        for(int side = 0; side < 2 && !err; ++side) {
            lua_getfield(L, 1, side ? "right" : "left");
            if(lua_istable(L, -1)) {
                names.clear();
                if(read_sl_list(L, lua_gettop(L), side, names))
                    (side ? lay.right : lay.left) = names;
                else err = "winter.statusline.setup: left/right are lists of "
                           "component names or functions";
            } else if(!lua_isnil(L, -1)) {
                err = "winter.statusline.setup: left/right must be lists";
            }
            lua_pop(L, 1);
        }
        lua_getfield(L, 1, "separators");
        if(!err) {
            if(lua_isboolean(L, -1)) {
                if(!lua_toboolean(L, -1)) sep_preset("none", lay.sep_left, lay.sep_right);
                else sep_preset("auto", lay.sep_left, lay.sep_right);
            } else if(lua_type(L, -1) == LUA_TSTRING) {
                if(!sep_preset(lua_tostring(L, -1), lay.sep_left, lay.sep_right))
                    err = "winter.statusline.setup: separators preset is "
                          "powerline|round|slant|none|auto";
            } else if(lua_istable(L, -1)) {
                for(int side = 0; side < 2; ++side) {
                    lua_getfield(L, -1, side ? "right" : "left");
                    auto& dst = side ? lay.sep_right : lay.sep_left;
                    if(lua_type(L, -1) == LUA_TSTRING) dst = lua_tostring(L, -1);
                    else if(lua_isboolean(L, -1)) {
                        if(lua_toboolean(L, -1)) dst.reset();
                        else dst = "";
                    }
                    lua_pop(L, 1);
                }
            } else if(!lua_isnil(L, -1)) {
                err = "winter.statusline.setup: bad separators";
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, 1, "enabled");
        if(lua_isboolean(L, -1)) lay.enabled = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    if(err) return luaL_error(L, "%s", err);
    return 0;
}

int l_sl_remove(lua_State* L) {
    lua_pushboolean(L, statusline_remove(luaL_checkstring(L, 1)));
    return 1;
}

void push_names(lua_State* L, const std::vector<std::string>& v) {
    lua_createtable(L, static_cast<int>(v.size()), 0);
    lua_Integer i = 1;
    for(const std::string& s : v) {
        lua_pushlstring(L, s.data(), s.size());
        lua_rawseti(L, -2, i++);
    }
}

int l_sl_list(lua_State* L) {
    push_names(L, statusline_names());
    return 1;
}

// -> { left = {...}, right = {...}, enabled = bool }
int l_sl_layout(lua_State* L) {
    const StatusLayout& lay = statusline_layout();
    lua_createtable(L, 0, 3);
    push_names(L, lay.left);
    lua_setfield(L, -2, "left");
    push_names(L, lay.right);
    lua_setfield(L, -2, "right");
    set_bool(L, "enabled", lay.enabled);
    return 1;
}

int l_sl_reset(lua_State*) {
    for(auto& v : g_sl_anon) {
        for(const std::string& n : v) statusline_remove(n);
        v.clear();
    }
    statusline_reset_layout();
    return 0;
}

// ---- winter.buf extras -----------------------------------------------------

void push_buf_info(lua_State* L, size_t i) {
    const Buffer& b = *E.buffers[i];
    lua_createtable(L, 0, 8);
    set_int(L, "index", static_cast<lua_Integer>(i) + 1);
    set_str(L, "name", b.filename);
    set_str(L, "lang", b.syntax.lang());
    set_bool(L, "modified", b.pt.modified());
    set_int(L, "lines", static_cast<lua_Integer>(b.pt.line_count()));
    set_bool(L, "current", i == E.cur);
    set_bool(L, "nofile", b.nofile);
}

// 1-based buffer argument (default: current); 0 = out of range
size_t buf_arg(lua_State* L, int i) {
    const lua_Integer n = luaL_optinteger(L, i, static_cast<lua_Integer>(E.cur) + 1);
    if(n < 1 || n > static_cast<lua_Integer>(E.buffers.size())) return 0;
    return static_cast<size_t>(n);
}

int l_buf_list(lua_State* L) {
    lua_createtable(L, static_cast<int>(E.buffers.size()), 0);
    for(size_t i = 0; i < E.buffers.size(); ++i) {
        push_buf_info(L, i);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
    }
    return 1;
}

int l_buf_info(lua_State* L) {
    const size_t n = buf_arg(L, 1);
    if(!n) {
        lua_pushnil(L);
        return 1;
    }
    push_buf_info(L, n - 1);
    return 1;
}

int l_buf_current(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(E.cur) + 1);
    return 1;
}

// winter.buf.switch(index | "name") -> bool
int l_buf_switch(lua_State* L) {
    if(lua_type(L, 1) == LUA_TSTRING) {
        lua_pushboolean(L, buffer_goto(lua_tostring(L, 1)));
        return 1;
    }
    const size_t n = buf_arg(L, 1);
    if(n) buffer_switch(n - 1);
    lua_pushboolean(L, n != 0);
    return 1;
}

// winter.buf.open(path) -> index
int l_buf_open(lua_State* L) {
    const size_t i = buffer_open(luaL_checkstring(L, 1));
    lua_pushinteger(L, static_cast<lua_Integer>(i) + 1);
    return 1;
}

// winter.buf.close([index [, force]]) -> bool
int l_buf_close(lua_State* L) {
    const size_t n = buf_arg(L, 1);
    const bool force = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, n && buffer_close(n - 1, force));
    return 1;
}

int l_buf_cycle(lua_State* L) {
    buffer_cycle(static_cast<int>(luaL_optinteger(L, 1, 1)));
    return 0;
}

int l_buf_alt(lua_State*) {
    buffer_goto_alt();
    return 0;
}

int l_buf_save(lua_State*) {
    editor_save();
    return 0;
}

int l_buf_save_all(lua_State*) {
    editor_save_all();
    return 0;
}

int l_buf_modified(lua_State* L) {
    const size_t n = buf_arg(L, 1);
    lua_pushboolean(L, n && E.buffers[n - 1]->pt.modified());
    return 1;
}

// ---- winter.win ------------------------------------------------------------

int l_win_size(lua_State* L) {  // text rows, columns
    lua_pushinteger(L, E.term_rows);
    lua_pushinteger(L, E.term_cols);
    return 2;
}

int l_win_top(lua_State* L) {  // first visible line
    lua_pushinteger(L, E.row_offset + 1);
    return 1;
}

// ---- winter.job ------------------------------------------------------------

LuaFn fn_field(lua_State* L, int t, const char* key) {
    if(!t) return nullptr;
    lua_getfield(L, t, key);
    LuaFn f;
    if(lua_isfunction(L, -1)) f = lua_fn(L, -1);
    lua_pop(L, 1);
    return f;
}

// winter.job.start(cmd, {
//   on_stdout = fn(chunk), on_stderr = fn(chunk), on_exit = fn(code, output),
//   stdin = "text", pipe_stdin = bool, keep_output = bool })  -> id | nil
// Runs through the shell, async. Without on_stdout the whole output is handed
// to on_exit.
int l_job_start(lua_State* L) {
    size_t n;
    const char* cmd = luaL_checklstring(L, 1, &n);
    const int t = table_arg(L, 2);
    JobId id = 0;
    {
        JobSpec spec;
        spec.cmd.assign(cmd, n);
        const LuaFn on_out = fn_field(L, t, "on_stdout");
        const LuaFn on_err = fn_field(L, t, "on_stderr");
        const LuaFn on_exit = fn_field(L, t, "on_exit");
        if(t) {
            lua_getfield(L, t, "stdin");
            if(lua_type(L, -1) == LUA_TSTRING) {
                size_t sn;
                const char* s = lua_tolstring(L, -1, &sn);
                spec.input = std::string(s, sn);
            }
            lua_pop(L, 1);
        }
        spec.pipe_stdin = lua_opt_bool(L, t, "pipe_stdin", false);
        spec.keep_out = lua_opt_bool(L, t, "keep_output", !on_out);
        spec.split_err = on_err != nullptr;
        const auto chunk = [](LuaFn f) {
            return [f](std::string_view d) {
                lua_State* L = script_state();
                if(!L || !f) return;
                f->push();
                lua_pushlstring(L, d.data(), d.size());
                script_call(1);
            };
        };
        if(on_out) spec.on_data = chunk(on_out);
        if(on_err) spec.on_err = chunk(on_err);
        if(on_exit)
            spec.on_exit = [on_exit](JobResult r) {
                lua_State* L = script_state();
                if(!L) return;
                on_exit->push();
                lua_pushinteger(L, r.code);
                lua_pushlstring(L, r.out.data(), r.out.size());
                script_call(2);
            };
        id = job_start(std::move(spec));
    }
    if(id) lua_pushinteger(L, id);
    else lua_pushnil(L);
    return 1;
}

// winter.job.run(cmd, fn(output, code) [, stdin])
int l_job_run(lua_State* L) {
    size_t n;
    const char* cmd = luaL_checklstring(L, 1, &n);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    JobId id = 0;
    {
        JobSpec spec;
        spec.cmd.assign(cmd, n);
        if(lua_type(L, 3) == LUA_TSTRING) {
            size_t sn;
            const char* s = lua_tolstring(L, 3, &sn);
            spec.input = std::string(s, sn);
        }
        spec.on_exit = [f = lua_fn(L, 2)](JobResult r) {
            lua_State* L = script_state();
            if(!L) return;
            f->push();
            lua_pushlstring(L, r.out.data(), r.out.size());
            lua_pushinteger(L, r.code);
            script_call(2);
        };
        id = job_start(std::move(spec));
    }
    if(id) lua_pushinteger(L, id);
    else lua_pushnil(L);
    return 1;
}

int l_job_write(lua_State* L) {
    size_t n;
    const char* s = luaL_checklstring(L, 2, &n);
    lua_pushboolean(
        L, job_write(static_cast<JobId>(luaL_checkinteger(L, 1)), {s, n}));
    return 1;
}

int l_job_close_stdin(lua_State* L) {
    job_close_stdin(static_cast<JobId>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_job_kill(lua_State* L) {
    job_kill(static_cast<JobId>(luaL_checkinteger(L, 1)));
    return 0;
}

// ---- winter.timer ----------------------------------------------------------

struct LuaTimer {
    int id = 0, ms = 0;
    bool repeat = false;
    LuaFn fn;
    TimerId cur = 0;
};
std::map<int, std::shared_ptr<LuaTimer>> g_timers;
int g_next_timer = 1;

void arm_timer(const std::shared_ptr<LuaTimer>& t) {
    t->cur = loop_after(t->ms, [t] {
        t->cur = 0;
        if(!g_timers.contains(t->id)) return;
        if(!t->repeat) g_timers.erase(t->id);
        if(lua_State* L = script_state()) {
            (void)L;
            t->fn->push();
            script_call(0);
        }
        // the callback may have cancelled it
        if(t->repeat && g_timers.contains(t->id)) arm_timer(t);
    });
}

int new_timer(lua_State* L, bool repeat) {
    const lua_Integer ms = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    auto t = std::make_shared<LuaTimer>();
    t->id = g_next_timer++;
    t->ms = static_cast<int>(std::max<lua_Integer>(ms, 1));
    t->repeat = repeat;
    t->fn = lua_fn(L, 2);
    g_timers[t->id] = t;
    arm_timer(t);
    lua_pushinteger(L, t->id);
    return 1;
}

int l_timer_after(lua_State* L) { return new_timer(L, false); }
int l_timer_every(lua_State* L) { return new_timer(L, true); }

int l_timer_cancel(lua_State* L) {
    const auto it = g_timers.find(static_cast<int>(luaL_checkinteger(L, 1)));
    const bool found = it != g_timers.end();
    if(found) {
        if(it->second->cur) loop_cancel(it->second->cur);
        g_timers.erase(it);
    }
    lua_pushboolean(L, found);
    return 1;
}

// ---- winter.picker / winter.ui.select --------------------------------------

LuaFn g_pick_items;  // the table of the open picker; accept hands back its entry

// items: strings or { text=, detail=, path=, line=, col= }; PickItem::id is the
// 1-based index into the Lua list.
bool read_pick_items(lua_State* L, int idx, std::vector<PickItem>& out) {
    idx = lua_absindex(L, idx);
    const lua_Unsigned n = lua_rawlen(L, idx);
    for(lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
        PickItem it;
        it.id = static_cast<size_t>(i);
        bool ok = true;
        if(lua_type(L, -1) == LUA_TSTRING) {
            it.text = lua_tostring(L, -1);
        } else if(lua_istable(L, -1)) {
            const int t = lua_gettop(L);
            const auto str = [&](const char* k, std::string& dst) {
                lua_getfield(L, t, k);
                if(lua_type(L, -1) == LUA_TSTRING) dst = lua_tostring(L, -1);
                lua_pop(L, 1);
            };
            const auto num = [&](const char* k, int& dst) {
                lua_getfield(L, t, k);
                if(lua_isinteger(L, -1)) dst = static_cast<int>(lua_tointeger(L, -1));
                lua_pop(L, 1);
            };
            str("text", it.text);
            str("detail", it.detail);
            str("path", it.path);
            num("line", it.line);
            num("col", it.col);
            ok = !it.text.empty();
        } else {
            ok = false;
        }
        lua_pop(L, 1);
        if(!ok) return false;
        out.push_back(std::move(it));
    }
    return true;
}

// winter.picker.open({
//   prompt = "Pick", items = {...}, fuzzy = true, preview = false,
//   paths = false, max_results = 200,
//   on_accept = fn(item, index), on_close = fn(),
//   on_query = fn(text)   -- fuzzy = false: answer with winter.picker.set_items
// })
int l_picker_open(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    bool ok = true;
    {
        PickerSpec spec;
        std::vector<PickItem> items;
        lua_getfield(L, 1, "prompt");
        spec.prompt = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "Pick";
        lua_pop(L, 1);
        spec.fuzzy = lua_opt_bool(L, 1, "fuzzy", true);
        spec.preview = lua_opt_bool(L, 1, "preview", false);
        spec.paths = lua_opt_bool(L, 1, "paths", false);
        lua_getfield(L, 1, "max_results");
        if(lua_isinteger(L, -1))
            spec.max_results = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 1, "items");
        LuaFn list;
        if(lua_istable(L, -1)) {
            ok = read_pick_items(L, lua_gettop(L), items);
            list = lua_fn(L, -1);
        }
        lua_pop(L, 1);

        if(ok) {
            const LuaFn on_accept = fn_field(L, 1, "on_accept");
            const LuaFn on_close = fn_field(L, 1, "on_close");
            const LuaFn on_query = fn_field(L, 1, "on_query");
            if(on_accept)
                spec.on_accept = [on_accept](const PickItem& it) {
                    lua_State* L = script_state();
                    if(!L) return;
                    on_accept->push();
                    if(g_pick_items) {
                        g_pick_items->push();
                        lua_rawgeti(L, -1, static_cast<lua_Integer>(it.id));
                        lua_remove(L, -2);
                    } else {
                        lua_pushlstring(L, it.text.data(), it.text.size());
                    }
                    lua_pushinteger(L, static_cast<lua_Integer>(it.id));
                    script_call(2);
                };
            if(on_close)
                spec.on_close = [on_close] {
                    lua_State* L = script_state();
                    if(!L) return;
                    on_close->push();
                    script_call(0);
                };
            if(on_query)
                spec.on_query = [on_query](std::string_view q) {
                    lua_State* L = script_state();
                    if(!L) return;
                    on_query->push();
                    lua_pushlstring(L, q.data(), q.size());
                    script_call(1);
                };
            g_pick_items = list;
            picker_open(std::move(spec));
            if(!items.empty()) picker_add(std::move(items));
        }
    }
    if(!ok)
        return luaL_error(L,
                          "winter.picker.open: items are strings or "
                          "{text=, detail=, path=, line=, col=}");
    return 0;
}

// winter.picker.set_items(list): replaces what the open picker shows
int l_picker_set_items(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    bool ok = true, active = picker_active();
    {
        std::vector<PickItem> items;
        ok = read_pick_items(L, 1, items);
        if(ok && active) {
            g_pick_items = lua_fn(L, 1);
            picker_reset();
            if(!items.empty()) picker_add(std::move(items));
        }
    }
    if(!ok) return luaL_error(L, "winter.picker.set_items: bad item");
    lua_pushboolean(L, active);
    return 1;
}

int l_picker_close(lua_State*) {
    picker_close();
    return 0;
}

int l_picker_active(lua_State* L) {
    lua_pushboolean(L, picker_active());
    return 1;
}

int l_picker_files(lua_State*) {
    picker_files();
    return 0;
}

int l_picker_grep(lua_State*) {
    picker_grep();
    return 0;
}

// winter.ui.select(items, {prompt=, format=fn(item)->string}, on_choice)
// on_choice(item, index) -- or on_choice(nil) when cancelled. nvim-style.
int l_ui_select(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const int t = table_arg(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    bool ok = true;
    {
        PickerSpec spec;
        std::vector<PickItem> items;
        spec.prompt = "Select";
        if(t) {
            lua_getfield(L, t, "prompt");
            if(lua_type(L, -1) == LUA_TSTRING) spec.prompt = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        const LuaFn format = fn_field(L, t, "format");
        const lua_Unsigned n = lua_rawlen(L, 1);
        for(lua_Unsigned i = 1; i <= n && ok; ++i) {
            lua_rawgeti(L, 1, static_cast<lua_Integer>(i));
            PickItem it;
            it.id = static_cast<size_t>(i);
            if(format) {
                format->push();
                lua_pushvalue(L, -2);
                if(script_call(1, 1) && lua_type(L, -1) == LUA_TSTRING)
                    it.text = lua_tostring(L, -1);
                lua_pop(L, 1);
            } else if(lua_type(L, -1) == LUA_TSTRING ||
                      lua_type(L, -1) == LUA_TNUMBER) {
                it.text = lua_tostring(L, -1);
            } else if(lua_istable(L, -1)) {
                for(const char* k : {"text", "name", "label"}) {
                    lua_getfield(L, -1, k);
                    if(it.text.empty() && lua_type(L, -1) == LUA_TSTRING)
                        it.text = lua_tostring(L, -1);
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
            if(it.text.empty()) it.text = std::format("<item {}>", i);
            items.push_back(std::move(it));
        }
        const LuaFn choose = lua_fn(L, 3);
        const auto accepted = std::make_shared<bool>(false);
        g_pick_items = lua_fn(L, 1);
        spec.fuzzy = true;
        spec.on_accept = [choose, accepted](const PickItem& it) {
            *accepted = true;
            lua_State* L = script_state();
            if(!L) return;
            choose->push();
            g_pick_items->push();
            lua_rawgeti(L, -1, static_cast<lua_Integer>(it.id));
            lua_remove(L, -2);
            lua_pushinteger(L, static_cast<lua_Integer>(it.id));
            script_call(2);
        };
        // on_close runs before on_accept, so decide "cancelled" a tick later
        spec.on_close = [choose, accepted] {
            loop_post([choose, accepted] {
                if(*accepted) return;
                lua_State* L = script_state();
                if(!L) return;
                choose->push();
                lua_pushnil(L);
                script_call(1);
            });
        };
        picker_open(std::move(spec));
        picker_add(std::move(items));
    }
    (void)ok;
    return 0;
}

// ---- winter.diag -----------------------------------------------------------

// winter.diag.get([bufnr]) -> { {line=, col=, end_line=, end_col=, severity=1..4,
//   severity_name=, message=, source=}, ... }
int l_diag_get(lua_State* L) {
    const size_t n = buf_arg(L, 1);
    if(!n) {
        lua_newtable(L);
        return 1;
    }
    const Buffer& b = *E.buffers[n - 1];
    const PieceTable& pt = b.pt;
    lua_createtable(L, static_cast<int>(b.diags.size()), 0);
    lua_Integer i = 1;
    for(const Diag& d : b.diags) {
        const size_t s = std::min(d.start, pt.size());
        const size_t e = std::min(std::max(d.end, s), pt.size());
        const size_t sl = pt.line_of(s), el = pt.line_of(e);
        lua_createtable(L, 0, 8);
        set_int(L, "line", static_cast<lua_Integer>(sl) + 1);
        set_int(L, "col", static_cast<lua_Integer>(s - pt.line_start(sl)) + 1);
        set_int(L, "end_line", static_cast<lua_Integer>(el) + 1);
        set_int(L, "end_col", static_cast<lua_Integer>(e - pt.line_start(el)) + 1);
        set_int(L, "severity", d.severity);
        set_str(L, "severity_name", diag_name(d.severity));
        set_str(L, "message", d.msg);
        set_str(L, "source", d.source);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// winter.diag.count([bufnr]) -> { error=, warn=, info=, hint= }
int l_diag_count(lua_State* L) {
    int c[5] = {};
    if(const size_t n = buf_arg(L, 1))
        for(const Diag& d : E.buffers[n - 1]->diags)
            if(d.severity >= 1 && d.severity <= 4) ++c[d.severity];
    lua_createtable(L, 0, 4);
    set_int(L, "error", c[1]);
    set_int(L, "warn", c[2]);
    set_int(L, "info", c[3]);
    set_int(L, "hint", c[4]);
    return 1;
}

// winter.diag.jump(dir [, count]): dir > 0 next, < 0 previous
int l_diag_jump(lua_State* L) {
    diag_jump(luaL_checkinteger(L, 1) < 0 ? -1 : 1,
              static_cast<int>(luaL_optinteger(L, 2, 1)));
    return 0;
}

// ---- winter.options --------------------------------------------------------

bool push_option_value(lua_State* L, std::string_view name) {
    const auto v = get_option(name);
    if(!v) return false;
    if(*v == "true") lua_pushboolean(L, 1);
    else if(*v == "false") lua_pushboolean(L, 0);
    else if(!v->empty() && std::isdigit(static_cast<unsigned char>(v->front())))
        lua_pushinteger(L, std::atoll(v->c_str()));
    else lua_pushlstring(L, v->data(), v->size());
    return true;
}

void push_option_info(lua_State* L, const OptInfo& i) {
    lua_createtable(L, 0, 6);
    set_str(L, "name", i.name);
    if(!i.alias.empty()) set_str(L, "alias", i.alias);
    set_str(L, "type", i.type);
    if(i.type == "integer") {
        set_int(L, "min", i.lo);
        set_int(L, "max", i.hi);
    }
    push_option_value(L, i.name);
    lua_setfield(L, -2, "value");
}

int l_options_list(lua_State* L) {
    const auto names = option_names();
    lua_createtable(L, static_cast<int>(names.size()), 0);
    lua_Integer k = 1;
    for(const std::string_view n : names) {
        if(const auto i = option_info(n)) {
            push_option_info(L, *i);
            lua_rawseti(L, -2, k++);
        }
    }
    return 1;
}

int l_options_info(lua_State* L) {
    const auto i = option_info(luaL_checkstring(L, 1));
    if(!i) lua_pushnil(L);
    else push_option_info(L, *i);
    return 1;
}

int l_options_get(lua_State* L) {
    if(!push_option_value(L, luaL_checkstring(L, 1))) lua_pushnil(L);
    return 1;
}

// winter.options.set(name, value) -> true | false, "reason"   (never throws)
int l_options_set(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const int t = lua_type(L, 2);
    if(t != LUA_TBOOLEAN && t != LUA_TNUMBER && t != LUA_TSTRING)
        return luaL_argerror(L, 2, "expected number, boolean or string");
    OptResult r;
    {
        std::string value;
        if(t == LUA_TBOOLEAN) value = lua_toboolean(L, 2) ? "true" : "false";
        else if(t == LUA_TNUMBER)
            value = std::to_string(static_cast<long long>(lua_tonumber(L, 2)));
        else value = lua_tostring(L, 2);
        r = set_option(name, value);
    }
    if(r == OptResult::Ok) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    lua_pushstring(L, r == OptResult::Unknown ? "unknown option" : "invalid value");
    return 2;
}

// winter.options.toggle(name) -> new value
int l_options_toggle(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto v = get_option(name);
    if(!v || (*v != "true" && *v != "false"))
        return luaL_error(L, "'%s' is not a boolean option", name);
    set_option(name, *v == "true" ? "false" : "true");
    push_option_value(L, name);
    return 1;
}

// ---- winter.lsp extras -----------------------------------------------------

int l_lsp_rename(lua_State* L) {
    lsp_rename(luaL_optstring(L, 1, ""));
    return 0;
}

const luaL_Reg kStatusline[] = {{"setup", l_sl_setup},
                                {"component", l_sl_component},
                                {"remove", l_sl_remove},
                                {"list", l_sl_list},
                                {"layout", l_sl_layout},
                                {"reset", l_sl_reset},
                                {nullptr, nullptr}};
const luaL_Reg kBufExt[] = {{"list", l_buf_list},
                            {"info", l_buf_info},
                            {"current", l_buf_current},
                            {"switch", l_buf_switch},
                            {"open", l_buf_open},
                            {"close", l_buf_close},
                            {"cycle", l_buf_cycle},
                            {"alt", l_buf_alt},
                            {"save", l_buf_save},
                            {"save_all", l_buf_save_all},
                            {"modified", l_buf_modified},
                            {nullptr, nullptr}};
const luaL_Reg kWin[] = {
    {"size", l_win_size}, {"top", l_win_top}, {nullptr, nullptr}};
const luaL_Reg kJob[] = {{"start", l_job_start},
                         {"run", l_job_run},
                         {"write", l_job_write},
                         {"close_stdin", l_job_close_stdin},
                         {"kill", l_job_kill},
                         {nullptr, nullptr}};
const luaL_Reg kTimer[] = {{"after", l_timer_after},
                           {"every", l_timer_every},
                           {"cancel", l_timer_cancel},
                           {nullptr, nullptr}};
const luaL_Reg kPicker[] = {{"open", l_picker_open},
                            {"set_items", l_picker_set_items},
                            {"close", l_picker_close},
                            {"active", l_picker_active},
                            {"files", l_picker_files},
                            {"grep", l_picker_grep},
                            {nullptr, nullptr}};
const luaL_Reg kUiExt[] = {{"select", l_ui_select}, {nullptr, nullptr}};
const luaL_Reg kDiag[] = {{"get", l_diag_get},
                          {"count", l_diag_count},
                          {"jump", l_diag_jump},
                          {nullptr, nullptr}};
const luaL_Reg kOptions[] = {{"list", l_options_list},
                             {"info", l_options_info},
                             {"get", l_options_get},
                             {"set", l_options_set},
                             {"toggle", l_options_toggle},
                             {nullptr, nullptr}};
const luaL_Reg kLspExt[] = {{"rename", l_lsp_rename}, {nullptr, nullptr}};
