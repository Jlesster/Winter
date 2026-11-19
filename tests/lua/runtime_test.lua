-- ---- mock of the native winter.* the runtime sits on -------------------------
local function mock()
  local W = {}
  W.B = {
    { id = 11, name = "a.lua", lang = "lua", lines = { "one", "two", "three" }, cy = 0, cx = 0 },
    { id = 12, name = "b.rs",  lang = "rust", lines = { "fn main() {}" }, cy = 0, cx = 0 },
  }
  W.cur = 1
  local function find(id)
    if id == 0 then return W.B[W.cur] end
    for _, b in ipairs(W.B) do if b.id == id then return b end end
  end
  local function need(id)
    local b = find(id); if not b then error("invalid buffer id " .. id, 2) end; return b
  end
  W.status, W.timers, W.jobs, W.attached = {}, {}, {}, {}
  W.handlers, W.next_h = {}, 1
  W.ui = { status = function(m) W.status[#W.status + 1] = m end }
  W.timer = { after = function(ms, fn) W.timers[#W.timers + 1] = { ms = ms, fn = fn }; return #W.timers end }
  W.job = { run = function(cmd, fn, stdin) W.jobs[#W.jobs + 1] = { cmd = cmd, fn = fn, stdin = stdin }; return #W.jobs end }
  local argless = { ready = 1, quit = 1, text_changed = 1, cursor_moved = 1, reload = 1 }
  W.ev = {
    on = function(name, fn, opts)
      if opts and opts.pattern and argless[name] then error("this event has no pattern to match", 2) end
      local id = W.next_h; W.next_h = W.next_h + 1
      W.handlers[id] = { name = name, fn = fn, opts = opts or {} }
      return id
    end,
    off = function(id) local had = W.handlers[id] ~= nil; W.handlers[id] = nil; return had end,
  }
  function W.fire(name, payload)  -- what the native layer does on events_emit
    payload = payload or {}
    payload.event = name
    payload.buf = payload.buf or W.B[W.cur].id
    local ids = {}
    for id in pairs(W.handlers) do ids[#ids + 1] = id end
    table.sort(ids)
    for _, id in ipairs(ids) do
      local h = W.handlers[id]
      if h and h.name == name then
        local pat, ok = h.opts.pattern, true
        if pat then
          ok = false
          for _, p in ipairs(type(pat) == "table" and pat or { pat }) do
            if name == "file_type" then ok = ok or payload.lang == p
            else ok = ok or (payload.filename or ""):match(("^" .. p:gsub("%.", "%%."):gsub("%*", ".*") .. "$")) ~= nil end
          end
        end
        if ok then h.fn(payload) end
      end
    end
  end
  -- windows: id, buffer (stable id), view; exactly one is focused
  W.wins = {}
  local function wfind(id)
    for _, w in ipairs(W.wins) do
      if id == 0 and w.focused or w.id == id then return w end
    end
  end
  local function wneed(id) local w = wfind(id); if not w then error("invalid window id", 2) end; return w end
  W.win = {
    current = function() return wfind(0).id end,
    list = function() local o = {}; for i, w in ipairs(W.wins) do o[i] = w.id end; return o end,
    info = function(id)
      local w = wfind(id); if not w then return nil end
      return { id = w.id, buf = w.buf, focused = w.focused or false, x = 0, y = 0, width = 80, height = 24,
               line = w.cy + 1, col = w.cx + 1, top = w.top + 1, left = 1 }
    end,
    get_cursor = function(id) local w = wneed(id); return w.cy + 1, w.cx + 1 end,
    set_cursor = function(id, l, c) local w = wneed(id); w.cy, w.cx = l - 1, (c or 1) - 1 end,
    get_buf = function(id) return wneed(id).buf end,
    set_buf = function(id, b) local w = wneed(id); if not find(b) then return false end; w.buf = b; return true end,
    set_top = function(id, l) wneed(id).top = l - 1 end,
    focus = function(id) local w = wneed(id); for _, x in ipairs(W.wins) do x.focused = false end; w.focused = true; return true end,
    split = function(opts)
      local cur = wfind(0); W.split_opts = opts
      local nw = { id = W.next_win, buf = cur.buf, cy = cur.cy, cx = cur.cx, top = cur.top }
      W.next_win = W.next_win + 1
      for _, x in ipairs(W.wins) do x.focused = false end
      nw.focused = true; table.insert(W.wins, nw)
      return nw.id
    end,
    close = function(id, force)
      local w = wneed(id)
      if #W.wins <= 1 then return false, "cannot close the last window" end
      if not force and w.buf == 99 then return false, "buffer has unsaved changes" end
      for i, x in ipairs(W.wins) do if x == w then table.remove(W.wins, i) break end end
      if w.focused then W.wins[1].focused = true end
      return true
    end,
  }
  W.next_win = 3
  -- winter.ts: buffers 41 (lua) and 42 (rust) have trees, everything else does not
  W.compiles = 0
  local tslang = { [41] = "lua", [42] = "rust" }
  W.ts = {
    lang = function(id) return tslang[id == 0 and 41 or id] end,
    root = function(id)
      id = id == 0 and 41 or id
      if not tslang[id] then return nil, "no such buffer" end
      return { lang = function() return tslang[id] end }
    end,
    node_at = function(id, l, c, o) W.node_at_args = { id, l, c, o }; return { type = function() return "node" end } end,
    query = function(lang, src)
      W.compiles = W.compiles + 1
      if src:find("BAD", 1, true) then return nil, "1:1: syntax error" end
      return { lang = lang, captures = function(_, root, opts) W.cap_opts = opts; return { { name = "a" } } end }
    end,
  }
  W.maps, W.unmaps = {}, {}
  W.keymap = {
    set = function(m, k, r, o) W.maps[#W.maps + 1] = { m = m, k = k, r = r, o = o } end,
    del = function(m, k, o) W.unmaps[#W.unmaps + 1] = { m = m, k = k, o = o }; return true end,
    list = function(o) W.listed = o; return { { mode = "n", keys = "gx", action = "a" } } end,
  }
  W.buf = {
    id = function(i) i = i or W.cur; return W.B[i] and W.B[i].id end,
    by_id = function(id) for i, b in ipairs(W.B) do if b.id == id then return i end end end,
    info = function(i) local b = W.B[i]; if not b then return nil end
      return { index = i, id = b.id, name = b.name, lang = b.lang, modified = b.mod or false, lines = #b.lines } end,
    list = function() local o = {}; for i in ipairs(W.B) do o[i] = W.buf.info(i) end; return o end,
    switch = function(i) W.switched = i; return true end,
    close = function(i, force) W.closed = { i, force }; return true end,
  }
  W.api = {
    buf_valid = function(id) return find(id) ~= nil end,
    buf_line_count = function(id) return #need(id).lines end,
    buf_get_name = function(id) return need(id).name end,
    buf_changedtick = function(id) return need(id).tick or 1 end,
    buf_get_lines = function(id, first, last)
      local b = need(id); local n = #b.lines
      first = math.max(first or 1, 1); last = last or -1
      if last < 0 then last = n + 1 + last end
      last = math.min(last, n)
      local o = {}; for l = first, last do o[#o + 1] = b.lines[l] end; return o
    end,
    buf_set_lines = function(id, first, last, new)
      local b = need(id); local n = #b.lines
      assert(first >= 1 and first <= n + 1, "line out of range")
      assert(last >= first - 1 and last <= n, "line out of range")
      local out = {}
      for i = 1, first - 1 do out[#out + 1] = b.lines[i] end
      for _, s in ipairs(new) do out[#out + 1] = s end
      for i = math.max(last, first - 1) + 1, n do out[#out + 1] = b.lines[i] end
      b.lines = out
    end,
    buf_get_text = function(id, l1, c1, l2, c2)
      local b = need(id)
      local function off(l, c) local o = 0; for i = 1, l - 1 do o = o + #b.lines[i] + 1 end
        return o + math.min(math.max(c - 1, 0), #b.lines[l]) end
      local all = table.concat(b.lines, "\n")
      return all:sub(off(l1, c1) + 1, off(l2, c2))
    end,
    buf_set_text = function(id, l1, c1, l2, c2, new)
      local b = need(id)
      local function off(l, c) local o = 0; for i = 1, l - 1 do o = o + #b.lines[i] + 1 end
        return o + math.min(math.max(c - 1, 0), #b.lines[l]) end
      local all = table.concat(b.lines, "\n")
      local res = all:sub(1, off(l1, c1)) .. table.concat(new, "\n") .. all:sub(off(l2, c2) + 1)
      b.lines = {}; for line in (res .. "\n"):gmatch("(.-)\n") do b.lines[#b.lines + 1] = line end
    end,
    buf_get_cursor = function(id) local b = need(id); return b.cy + 1, b.cx + 1 end,
    buf_set_cursor = function(id, l, c) local b = need(id); b.cy, b.cx = l - 1, (c or 1) - 1 end,
    buf_attach = function(id, opts) need(id); W.attached[#W.attached + 1] = { id = id, opts = opts }; return #W.attached end,
    buf_detach = function(h) W.detached = h; return true end,
  }
  return W
end

winter = mock()
dofile(arg[1])
local W = winter

-- ---- tiny test framework -----------------------------------------------------
local n, fail = 0, 0
local function ok(c, m) n = n + 1; if not c then fail = fail + 1; print("FAIL: " .. m) end end
local function eq(a, b, m)
  local sa = type(a) == "table" and winter.inspect(a) or tostring(a)
  local sb = type(b) == "table" and winter.inspect(b) or tostring(b)
  ok(sa == sb, m .. "\n   got  " .. sa .. "\n   want " .. sb)
end
local function throws(f, pat, m)
  local s, e = pcall(f)
  ok(not s and tostring(e):find(pat, 1, true), m .. " (got: " .. tostring(e) .. ")")
end

-- ---- tbl ---------------------------------------------------------------------
local tbl = winter.tbl
ok(tbl.islist({}) and tbl.islist({ 1, 2, 3 }) and not tbl.islist({ a = 1 }) and not tbl.islist({ 1, nil, 3 }) and not tbl.islist("x"), "islist")
ok(tbl.isempty({}) and not tbl.isempty({ 1 }), "isempty")
eq(tbl.count({ 1, 2, a = 3 }), 3, "count")
local ks = tbl.keys({ a = 1, b = 2 }); table.sort(ks); eq(ks, { "a", "b" }, "keys")
ok(tbl.contains({ 1, 2, 3 }, 2) and not tbl.contains({ 1, 2, 3 }, 9), "contains")
eq(tbl.map({ a = 1, b = 2 }, function(v) return v * 10 end), { a = 10, b = 20 }, "map")
eq(tbl.filter({ 1, 2, 3, 4 }, function(v) return v % 2 == 0 end), { 2, 4 }, "filter list stays list")
eq(tbl.filter({ a = 1, b = 2 }, function(v) return v > 1 end), { b = 2 }, "filter dict")
eq(tbl.slice({ 1, 2, 3, 4, 5 }, 2, 4), { 2, 3, 4 }, "slice"); eq(tbl.slice({ 1, 2, 3, 4, 5 }, -2), { 4, 5 }, "slice negative")
local orig = { a = { b = 1 }, l = { 1, 2 } }; orig.self = orig
local cp = tbl.deepcopy(orig)
ok(cp ~= orig and cp.a ~= orig.a and cp.a.b == 1 and cp.self == cp, "deepcopy copies and keeps cycles")
eq(tbl.extend("force", { a = 1, b = 2 }, { b = 3, c = 4 }), { a = 1, b = 3, c = 4 }, "extend force")
eq(tbl.extend("keep", { a = 1, b = 2 }, { b = 3, c = 4 }), { a = 1, b = 2, c = 4 }, "extend keep")
throws(function() tbl.extend("error", { a = 1 }, { a = 2 }) end, "more than one table", "extend error")
throws(function() tbl.extend("nope", {}) end, "behavior must be", "extend bad behavior")
local de = tbl.deep_extend("force", { a = { x = 1, y = 2 }, l = { 1, 2, 3 } }, { a = { y = 9, z = 3 }, l = { 7 } })
eq(de, { a = { x = 1, y = 9, z = 3 }, l = { 7 } }, "deep_extend merges tables, lists replace")
local src = { a = { b = 1 } }; local de2 = tbl.deep_extend("force", {}, src); de2.a.b = 2
eq(src.a.b, 1, "deep_extend does not alias sources")

-- ---- str ---------------------------------------------------------------------
local str = winter.str
eq(str.split("a,b,,c", ","), { "a", "b", "", "c" }, "split")
eq(str.split(",a,b,", ",", { trimempty = true }), { "a", "b" }, "split trimempty")
eq(str.split("a.b", "."), { "a", "b" }, "split is plain by default")
eq(str.split("a1b22c", "%d+", { plain = false }), { "a", "b", "c" }, "split pattern")
eq(str.split("abc", ""), { "a", "b", "c" }, "split chars")
eq(str.trim("  hi \n"), "hi", "trim")
ok(str.startswith("foobar", "foo") and not str.startswith("foobar", "bar") and str.endswith("foobar", "bar") and str.endswith("x", ""), "starts/endswith")
eq(str.shellescape("it's"), "'it'\\''s'", "shellescape")

-- ---- inspect -----------------------------------------------------------------
local I = winter.inspect
eq(I(1), "1", "inspect number"); eq(I("a\"b"), '"a\\"b"', "inspect string"); eq(I(nil), "nil", "inspect nil"); eq(I(true), "true", "inspect bool")
eq(I({ 1, 2, 3 }), "{ 1, 2, 3 }", "inspect list")
eq(I({ b = 1, a = { 2 }, [3] = "x" }), '{ [3] = "x", a = { 2 }, b = 1 }', "inspect sorted keys, number keys bracketed")
eq(I({ ["a b"] = 1 }), '{ ["a b"] = 1 }', "inspect non-identifier key")
eq(I({}), "{}", "inspect empty")
local cyc = {}; cyc.me = cyc
eq(I(cyc), "{ me = <cycle> }", "inspect cycle")
eq(I({ a = { b = { c = 1 } } }, { depth = 2 }), "{ a = { b = {...} } }", "inspect depth")
local wide = {}; for i = 1, 12 do wide[i] = "item" .. i end
local out = I(wide, { width = 40 })
ok(out:find("\n") and out:sub(1, 2) == "{\n" and out:sub(-1) == "}", "inspect wraps when too wide")
eq(I("a\nb"), '"a\\nb"', "inspect newline in string")

-- ---- validate ----------------------------------------------------------------
winter.validate { s = { "x", "string" }, n = { 1, "number" }, o = { nil, { "string", "nil" } }, f = { print, "function" } }
throws(function() winter.validate { name = { 5, "string" } } end, "bad argument 'name': expected string, got number", "validate type error")
throws(function() winter.validate { v = { true, { "string", "number" } } } end, "expected string or number, got boolean", "validate any-of")
winter.validate { p = { 5, function(v) return v > 0 end, "positive" } }
throws(function() winter.validate { p = { -1, function(v) return v > 0 end, "positive" } } end, "expected positive", "validate predicate")
winter.validate { c = { setmetatable({}, { __call = function() end }), "callable" } }
throws(function() winter.validate { c = { 5, "callable" } } end, "expected callable", "validate callable")
winter.validate { opt = { nil, "string", true } }

-- ---- notify / schedule / defer / system ---------------------------------------
winter.notify("hello"); winter.notify("careful", winter.log.levels.WARN); winter.notify("boom\nline2", winter.log.levels.ERROR)
winter.notify("quiet", winter.log.levels.DEBUG)
eq(W.status, { "hello", "warning: careful", "error: boom line2" }, "notify prefixes, collapses newlines, drops below level")
winter.log.level = 0; winter.notify("loud", 0); eq(W.status[#W.status], "loud", "log.level lowered"); winter.log.level = 2
local ran = 0; winter.schedule(function() ran = ran + 1 end)
ok(W.timers[1].ms == 1, "schedule uses the shortest delay"); W.timers[1].fn(); eq(ran, 1, "schedule runs fn")
winter.defer(function() end, 250); eq(W.timers[2].ms, 250, "defer ms"); winter.defer(function() end, 0); eq(W.timers[3].ms, 1, "defer clamps")
local res
winter.system({ "git", "log", "it's" }, { stdin = "in", cwd = "/my dir" }, function(r) res = r end)
eq(W.jobs[1].cmd, "cd '/my dir' && 'git' 'log' 'it'\\''s'", "system quotes list args and cwd"); eq(W.jobs[1].stdin, "in", "system stdin")
W.jobs[1].fn("OUT", 3); eq(res, { code = 3, stdout = "OUT" }, "system result table")
winter.system("ls | wc -l", function(r) res = r end); eq(W.jobs[2].cmd, "ls | wc -l", "system string passes through; opts optional")
throws(function() winter.system(42, function() end) end, "bad argument 'cmd'", "system validates cmd")
throws(function() winter.system("ls") end, "bad argument 'on_exit'", "system requires callback")

-- ---- variables -----------------------------------------------------------------
winter.g.x = 5; eq(winter.g.x, 5, "g")
winter.b[11].v = "a"; winter.b[12].v = "b"
eq(winter.b[11].v, "a", "b per buffer"); eq(winter.b[12].v, "b", "b isolates buffers")
ok(winter.b[999] == nil, "b of an invalid id is nil")
eq(winter.b[0].v, "a", "b[0] is the current buffer's table")
table.remove(W.B, 2); winter.b[11].touch = 1
local rawcount = 0; -- closed buffer's vars are pruned on the next creation
table.insert(W.B, { id = 13, name = "c", lang = "", lines = { "" }, cy = 0, cx = 0 })
ok(winter.b[13] ~= nil and winter.b[12] == nil, "table of an invalid buffer id is nil")
table.insert(W.B, { id = 12, name = "again", lang = "", lines = { "" }, cy = 0, cx = 0 })
ok(winter.b[12].v == nil, "vars of a closed buffer were pruned (a reused id starts clean)")
table.remove(W.B)  -- remove the re-added 12 again

-- ---- autocmd -------------------------------------------------------------------
local ac = winter.autocmd
local seen = {}
local id1 = ac.create("BufEnter", { group = ac.group("g1"), callback = function(e) seen[#seen + 1] = e.filename end, pattern = "*.lua" })
W.fire("buf_enter", { filename = "x.lua" }); W.fire("buf_enter", { filename = "x.rs" })
eq(seen, { "x.lua" }, "alias + pattern passthrough")
local got
ac.create("buf_close", { callback = function(e) got = e end })
W.fire("buf_close", { filename = "q", buf = 42 })
eq({ got.event, got.buf, got.filename }, { "buf_close", 42, "q" }, "payload has event, buf and named fields")
-- once
local c = 0; ac.create("TextChanged", { once = true, callback = function() c = c + 1 end })
W.fire("text_changed"); W.fire("text_changed"); eq(c, 1, "once")
-- returning true removes
c = 0; ac.create("cursor_moved", { callback = function() c = c + 1; return c >= 2 end })
for _ = 1, 4 do W.fire("cursor_moved") end; eq(c, 2, "callback returning true removes the autocmd")
-- derived insert events
local log = {}
ac.create("InsertEnter", { callback = function() log[#log + 1] = "enter" end })
ac.create("InsertLeave", { callback = function() log[#log + 1] = "leave" end })
W.fire("mode_changed", { old = "normal", new = "insert" }); W.fire("mode_changed", { old = "insert", new = "normal" }); W.fire("mode_changed", { old = "normal", new = "visual" })
eq(log, { "enter", "leave" }, "InsertEnter/InsertLeave derive from mode_changed")
c = 0; ac.create("visual_enter", { once = true, callback = function() c = c + 1 end })
W.fire("mode_changed", { old = "normal", new = "insert" }); eq(c, 0, "once on a derived event waits for its predicate")
W.fire("mode_changed", { old = "normal", new = "visual" }); W.fire("mode_changed", { old = "normal", new = "visual" }); eq(c, 1, "then fires once")
throws(function() ac.create("InsertEnter", { pattern = "x", callback = print }) end, "has no pattern", "pattern on derived event rejected")
throws(function() ac.create("text_changed", { pattern = "x", callback = print }) end, "no pattern", "pattern on argless event rejected")
throws(function() ac.create("BufEnter", {}) end, "bad argument 'callback'", "callback required")
-- multiple events
local evs = {}
ac.create({ "BufEnter", "FileType" }, { callback = function(e) evs[#evs + 1] = e.event end })
W.fire("buf_enter", { filename = "z" }); W.fire("file_type", { filename = "z", lang = "lua" })
eq(evs, { "buf_enter", "file_type" }, "list of events; ev.event is the winter name")
-- buffer filter
local hits = {}
ac.create("buf_enter", { buffer = 11, callback = function(e) hits[#hits + 1] = e.buf end })
ac.create("buf_enter", { buffer = true, callback = function(e) hits[#hits + 1] = "cur" .. e.buf end })
W.fire("buf_enter", { filename = "n", buf = 11 }); W.fire("buf_enter", { filename = "n", buf = 99 })
eq(hits, { 11, "cur11" }, "buffer = id and buffer = true filter by ev.buf")
-- groups: re-creating clears, clear=false keeps
local gh = {}
local function setup() ac.group("cfg"); ac.create("buf_enter", { group = "cfg", callback = function() gh[#gh + 1] = 1 end }) end
setup(); setup(); setup()
W.fire("buf_enter", { filename = "k" }); eq(#gh, 1, "group() clears, re-running setup does not stack handlers")
ac.group("cfg", { clear = false }); eq(#ac.list({ group = "cfg" }), 1, "clear=false keeps")
ac.create("buf_enter", { group = "auto-made", callback = print }); eq(#ac.list({ group = "auto-made" }), 1, "unknown group is created on first use")
-- list / del / clear
local listing = ac.list({ group = "cfg" })
eq({ listing[1].events, listing[1].group, listing[1].once }, { { "buf_enter" }, "cfg", false }, "list entry")
ok(ac.del(listing[1].id) and not ac.del(listing[1].id), "del true then false")
local before = #ac.list(); ac.clear({ event = "BufEnter" }); ok(#ac.list() < before and #ac.list({ event = "buf_enter" }) == 0, "clear by event (alias ok)")
ac.create("buf_enter", { group = "tmp", callback = print }); eq(ac.clear({ group = "tmp" }), 1, "clear by group returns count")
local native_before = 0; for _ in pairs(W.handlers) do native_before = native_before + 1 end
ac.clear(); local native_after = 0; for _ in pairs(W.handlers) do native_after = native_after + 1 end
ok(native_after < native_before and #ac.list() == 0, "clear removes the native subscriptions too")

-- ---- buffer handles ------------------------------------------------------------
W.B = { { id = 11, name = "a.lua", lang = "lua", lines = { "one", "two", "three" }, cy = 0, cx = 0 },
        { id = 12, name = "b.rs", lang = "rust", lines = { "fn main() {}" }, cy = 0, cx = 0 } }
W.cur = 1
local b = winter.buf.get()
ok(b.id == 11 and winter.buf.get(11) == b, "get() is the current buffer; handles are cached")
eq(tostring(b), "buf(11)", "tostring"); ok(winter.buf.get(777) == nil, "get of an invalid id is nil")
ok(b:is_valid() and b:index() == 1 and b:name() == "a.lua" and b:lang() == "lua" and b:line_count() == 3, "basic accessors")
eq(b:lines(), { "one", "two", "three" }, "lines()"); eq(b:lines(2, 2), { "two" }, "lines(a,b)"); eq(b:lines(2, -1), { "two", "three" }, "negative last")
eq(b:line(3), "three", "line()")
b:set_lines(2, 2, { "TWO", "2.5" }); eq(b:lines(), { "one", "TWO", "2.5", "three" }, "set_lines replaces")
b:append({ "four" }); eq(b:line(5), "four", "append")
b:set_lines(1, 0, { "zero" }); eq(b:line(1), "zero", "insert before")
b:set_lines(1, 1, {}); eq(b:line(1), "one", "delete")
local b2 = winter.buf.get(12)
b2:set_lines(1, 1, { "fn other() {}" }); eq(b2:line(1), "fn other() {}", "edit a NON-current buffer")
eq(b:line(1), "one", "...without touching the current one")
b2:set_text(1, 4, 1, 9, { "renamed" }); eq(b2:line(1), "fn renamed() {}", "set_text")
eq(b2:text(1, 4, 1, 11), "renamed", "text")
b2:set_cursor(1, 6); local cl, cc = b2:cursor(); eq({ cl, cc }, { 1, 6 }, "cursor")
b2:set_var("k", 7); eq(b2:var("k"), 7, "buffer vars"); eq(winter.b[12].k, 7, "...are winter.b[id]")
ok(b2:switch() and W.switched == 2, "switch maps id to index"); b2:close(true); eq(W.closed, { 2, true }, "close maps id to index and passes force")
eq(#winter.buf.handles(), 2, "handles() lists every buffer")
local h = b:attach({ on_edit = function() end }); ok(h == 1 and W.attached[1].id == 11, "attach passes the id"); b:detach(h); eq(W.detached, 1, "detach")
-- a closed buffer: methods raise a clear error, is_valid is false
table.remove(W.B, 2)
ok(not b2:is_valid(), "closed handle is invalid"); throws(function() b2:lines() end, "buffer 12 is closed", "closed handle raises")
-- index shifting: the handle still finds its buffer
table.insert(W.B, 1, { id = 20, name = "new", lang = "", lines = { "n" }, cy = 0, cx = 0 })
eq(b:index(), 2, "handle follows its buffer when indices shift"); eq(b:line(1), "one", "...and reads the right text")

-- ---- window handles --------------------------------------------------------------
W.B = { { id = 31, name = "a", lang = "lua", lines = { "x" }, cy = 0, cx = 0 },
        { id = 32, name = "b", lang = "rust", lines = { "y" }, cy = 0, cx = 0 } }
W.cur = 1
W.wins = { { id = 1, buf = 31, cy = 4, cx = 2, top = 0, focused = true },
           { id = 2, buf = 32, cy = 0, cx = 0, top = 0 } }
W.next_win = 3
local w1, w2 = winter.win.get(), winter.win.get(2)
ok(w1.id == 1 and w2.id == 2 and winter.win.get(1) == w1, "win.get(): focused window by default, handles cached")
eq(tostring(w1), "win(1)", "win tostring"); ok(winter.win.get(99) == nil, "invalid window id -> nil")
ok(w1:is_valid() and w1:is_focused() and not w2:is_focused(), "is_valid / is_focused")
eq({ w1:info().line, w1:info().col, w1:info().top }, { 5, 3, 1 }, "info carries the view")
local wd, ht = w1:size(); eq({ wd, ht }, { 80, 24 }, "size is text columns x rows")
eq(w1:buf().id, 31, "buf() is a buffer handle"); eq(w2:buf():name(), "b", "...a working one")
ok(w2:set_buf(w1:buf()) and w2:buf().id == 31, "set_buf takes a handle"); ok(w2:set_buf(32) and w2:buf().id == 32, "...or an id")
ok(w2:set_buf(777) == false, "set_buf of a missing buffer is false")
w2:set_cursor(3, 4); local wl, wc = w2:cursor(); eq({ wl, wc }, { 3, 4 }, "cursor round-trips")
w2:set_top(10); eq(w2:top(), 10, "set_top / top")
ok(w2:focus() and w2:is_focused() and not w1:is_focused() and winter.win.get().id == 2, "focus moves")
eq(#winter.win.handles(), 2, "handles() lists the windows"); eq(winter.win.handles()[1].id, 1, "...in layout order")
-- split returns a handle and focuses it; the handle is the one we split
local w3 = w1:split({ vertical = true })
ok(w3.id == 3 and w3:is_focused() and w3:buf().id == 31, "split returns the new focused window")
eq(W.split_opts.vertical, true, "split options pass through"); eq(#winter.win.handles(), 3, "three windows now")
-- closing: false + reason for the last window / unsaved, handle goes invalid
ok(w3:close() == true and not w3:is_valid(), "close removes the window")
throws(function() w3:cursor() end, "window 3 is closed", "closed handle raises")
throws(function() w3:focus() end, "window 3 is closed", "...for every method")
W.wins[2].buf = 99
local cr, cerr = w2:close(); ok(cr == false and cerr:find("unsaved"), "close refuses unsaved: " .. tostring(cerr))
ok(w2:close(true) == true, "force closes anyway")
local lr, lerr = w1:close(); ok(lr == false and lerr:find("last window"), "close refuses the last window")
-- autocmd aliases and the numeric ev.win
local wlog = {}
for _, name in ipairs({ "WinEnter", "WinLeave", "WinNew", "WinClosed" }) do
  winter.autocmd.create(name, { callback = function(e) wlog[#wlog + 1] = e.event .. ":" .. e.win end })
end
W.fire("win_enter", { win = 2 }); W.fire("win_leave", { win = 1 }); W.fire("win_new", { win = 3 }); W.fire("win_close", { win = 3 })
eq(wlog, { "win_enter:2", "win_leave:1", "win_new:3", "win_close:3" }, "WinEnter/WinLeave/WinNew/WinClosed aliases")
winter.autocmd.clear()
-- buffer / yank / lsp event aliases
local elog = {}
for _, name in ipairs({ "BufNew", "BufAdd", "TextYankPost", "LspAttach", "LspDetach" }) do
  winter.autocmd.create(name, { callback = function(e) elog[#elog + 1] = e.event end })
end
W.fire("buf_new", { filename = "x" }); W.fire("yank_post", { regname = "" }); W.fire("lsp_attach", { server = "clangd" }); W.fire("lsp_detach", { server = "clangd" })
eq(elog, { "buf_new", "buf_new", "yank_post", "lsp_attach", "lsp_detach" }, "BufNew/BufAdd/TextYankPost/LspAttach/LspDetach aliases")
winter.autocmd.clear()

-- ---- buffer-local keymaps ------------------------------------------------------------
W.B = { { id = 41, name = "a.lua", lang = "lua", lines = { "x" }, cy = 0, cx = 0 },
        { id = 42, name = "b.rs",  lang = "rust", lines = { "y" }, cy = 0, cx = 0 },
        { id = 43, name = "c.lua", lang = "lua", lines = { "z" }, cy = 0, cx = 0 } }
W.cur = 1
local kb = winter.buf.get(41)
local myopts = { desc = "hello" }
local fnmap = function() end
kb:map("n", "gx", fnmap, myopts)
eq({ W.maps[1].m, W.maps[1].k, W.maps[1].o.buffer, W.maps[1].o.desc }, { "n", "gx", 41, "hello" }, "Buf:map sets buffer = its id and keeps desc")
ok(W.maps[1].r == fnmap, "...and the rhs")
ok(myopts.buffer == nil, "Buf:map does not modify the caller's opts")
kb:map("nx", "gy", "action.name", { buffer = 999 })
eq(W.maps[2].o.buffer, 41, "a buffer the caller passed cannot override the handle's")
kb:map("i", "jk", "mode.normal"); eq(W.maps[3].o.buffer, 41, "opts are optional")
kb:unmap("n", "gx"); eq({ W.unmaps[1].k, W.unmaps[1].o.buffer }, { "gx", 41 }, "Buf:unmap targets the buffer")
local kl = kb:keymaps("n"); eq({ W.listed.buffer, W.listed.mode, kl[1].keys }, { 41, "n", "gx" }, "Buf:keymaps lists that buffer's maps")
W.B[1].gone = true; local gone = winter.buf.get(43); table.remove(W.B, 3)
throws(function() gone:map("n", "x", "y") end, "buffer 43 is closed", "a closed buffer's handle raises")
W.B[3] = { id = 43, name = "c.lua", lang = "lua", lines = { "z" }, cy = 0, cx = 0 }

-- keymap.ft: every buffer of the language, now and later
W.maps = {}
local ftid = winter.keymap.ft("lua", "n", "K", fnmap, { desc = "doc" })
local targets = {}
for _, m in ipairs(W.maps) do targets[#targets + 1] = m.o.buffer end
table.sort(targets); eq(targets, { 41, 43 }, "ft maps the open buffers of that language only")
ok(W.maps[1].o.desc == "doc" and W.maps[1].k == "K", "...with the options and keys given")
W.maps = {}
W.fire("file_type", { filename = "new.lua", lang = "lua", buf = 77 })
eq({ #W.maps, W.maps[1] and W.maps[1].o.buffer }, { 1, 77 }, "a buffer that becomes lua later gets the map")
W.fire("file_type", { filename = "x.rs", lang = "rust", buf = 78 })
eq(#W.maps, 1, "other languages are left alone")
ok(winter.autocmd.del(ftid), "ft returns an autocmd id")
W.fire("file_type", { filename = "y.lua", lang = "lua", buf = 79 }); eq(#W.maps, 1, "...which removes the hook")
-- several languages, several modes
W.maps = {}
winter.keymap.ft({ "lua", "rust" }, { "n", "v" }, "<C-j>", "some.action")
local both = {}; for _, m in ipairs(W.maps) do both[#both + 1] = m.o.buffer end
table.sort(both); eq(both, { 41, 42, 43 }, "a list of languages")
eq(W.maps[1].m, { "n", "v" }, "modes pass through unchanged")
throws(function() winter.keymap.ft(5, "n", "x", "y") end, "bad argument 'langs'", "ft validates langs")
throws(function() winter.keymap.ft("lua", "n", "x") end, "bad argument 'rhs'", "...and rhs")
winter.autocmd.clear()

-- ---- tree-sitter conveniences ------------------------------------------------------------
W.B = { { id = 41, name = "a.lua", lang = "lua", lines = { "x" }, cy = 0, cx = 0 },
        { id = 42, name = "b.rs", lang = "rust", lines = { "y" }, cy = 0, cx = 0 },
        { id = 43, name = "c.txt", lang = "", lines = { "z" }, cy = 0, cx = 0 } }
W.cur = 1
local c0 = W.compiles
local caps = winter.ts.captures(41, "(a) @a"); eq(#caps, 1, "ts.captures returns the captures"); eq(W.compiles - c0, 1, "compiled once")
winter.ts.captures(41, "(a) @a"); eq(W.compiles - c0, 1, "the compiled query is cached")
winter.ts.captures(0, "(a) @a"); eq(W.compiles - c0, 1, "buffer 0 is the current one (same language)")
winter.ts.captures(41, "(b) @b"); eq(W.compiles - c0, 2, "another source compiles again")
winter.ts.captures(42, "(a) @a"); eq(W.compiles - c0, 3, "another language compiles again")
winter.ts.captures(41, "(a) @a", { first = 2, limit = 5 }); eq({ W.cap_opts.first, W.cap_opts.limit }, { 2, 5 }, "opts are passed to the query")
local nr, nerr = winter.ts.captures(99, "(a) @a"); ok(nr == nil and nerr == "no such buffer", "a buffer without a tree: nil, err pass through")
local before = W.compiles
local br, berr = winter.ts.captures(41, "(BAD) @x"); ok(br == nil and berr == "1:1: syntax error", "a compile error is returned")
winter.ts.captures(41, "(BAD) @x"); eq(W.compiles - before, 2, "failed compiles are not cached")
throws(function() winter.ts.captures("x", "y") end, "bad argument 'buf'", "ts.captures validates buf")
throws(function() winter.ts.captures(41, 5) end, "bad argument 'source'", "...and source")
local tb = winter.buf.get(41)
eq(tb:ts_root():lang(), "lua", "Buf:ts_root"); tb:ts_node_at(2, 3, { named = true }); eq({ W.node_at_args[1], W.node_at_args[2], W.node_at_args[3], W.node_at_args[4].named }, { 41, 2, 3, true }, "Buf:ts_node_at passes the id")
eq(tb:ts_query("(a) @a").lang, "lua", "Buf:ts_query uses the buffer's language")
local nq, nqerr = winter.buf.get(43):ts_query("(a) @a"); ok(nq == nil and nqerr:find("no syntax tree"), "Buf:ts_query without a tree: " .. tostring(nqerr))
local tgone = winter.buf.get(43); table.remove(W.B, 3)
throws(function() tgone:ts_root() end, "buffer 43 is closed", "a closed handle raises")
local cc = W.compiles; winter._reset(); winter.ts.captures(41, "(a) @a"); eq(W.compiles - cc, 1, "_reset (config reload) clears the query cache")

-- ---- reset ---------------------------------------------------------------------
ac.create("buf_enter", { group = "z", callback = print }); ok(#ac.list() > 0, "something registered")
winter._reset(); eq(#ac.list(), 0, "_reset empties the autocmd registry")
ac.create("buf_enter", { callback = print }); eq(ac.list()[1].id, 1, "ids restart after reset")

print(("%d checks, %d failed"):format(n, fail))
os.exit(fail == 0 and 0 or 1)
