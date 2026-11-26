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

// The Lua half of the plugin API (winter.tbl/str/inspect/validate/notify/
// schedule/defer/system/autocmd, buffer handles, winter.g/b). Plain Lua text
// embedded here so the binary needs no runtime files; the source of truth is
// the string below. See docs/lua-api.md.
#include <lua.hpp>
#include <cstring>

#include "script_internal.hpp"

namespace {

const char kRuntime[] = R"LUA(-- winter runtime: the Lua half of the plugin API. Loaded once at startup, after
-- the native winter.* functions exist. Plain Lua 5.x, no host-only syntax.
local winter = winter
local unpack = table.unpack or unpack

-- ---------------------------------------------------------------------------
-- winter.tbl
-- ---------------------------------------------------------------------------
local tbl = {}
winter.tbl = tbl

function tbl.islist(t)
  if type(t) ~= "table" then return false end
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  for i = 1, n do
    if t[i] == nil then return false end
  end
  return true
end

function tbl.isempty(t) return next(t) == nil end

function tbl.count(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

function tbl.keys(t)
  local out = {}
  for k in pairs(t) do out[#out + 1] = k end
  return out
end

function tbl.values(t)
  local out = {}
  for _, v in pairs(t) do out[#out + 1] = v end
  return out
end

-- list membership (==)
function tbl.contains(t, value)
  for _, v in ipairs(t) do
    if v == value then return true end
  end
  return false
end

-- f over the values of a table, keys kept
function tbl.map(t, f)
  local out = {}
  for k, v in pairs(t) do out[k] = f(v) end
  return out
end

-- keeps the entries f(v) is true for; a list stays a list
function tbl.filter(t, f)
  local out = {}
  if tbl.islist(t) then
    for _, v in ipairs(t) do
      if f(v) then out[#out + 1] = v end
    end
  else
    for k, v in pairs(t) do
      if f(v) then out[k] = v end
    end
  end
  return out
end

-- list[i..j], negative indices count from the end
function tbl.slice(list, i, j)
  local n = #list
  i = i or 1
  j = j or n
  if i < 0 then i = n + 1 + i end
  if j < 0 then j = n + 1 + j end
  local out = {}
  for k = math.max(i, 1), math.min(j, n) do out[#out + 1] = list[k] end
  return out
end

function tbl.deepcopy(t, seen)
  if type(t) ~= "table" then return t end
  seen = seen or {}
  if seen[t] then return seen[t] end
  local out = {}
  seen[t] = out
  for k, v in pairs(t) do out[tbl.deepcopy(k, seen)] = tbl.deepcopy(v, seen) end
  return setmetatable(out, getmetatable(t))
end

local function extend(deep, behavior, ...)
  if behavior ~= "error" and behavior ~= "keep" and behavior ~= "force" then
    error("behavior must be 'error', 'keep' or 'force'", 3)
  end
  local out = {}
  for i = 1, select("#", ...) do
    local src = select(i, ...)
    if type(src) ~= "table" then
      error("argument " .. (i + 1) .. " must be a table", 3)
    end
    for k, v in pairs(src) do
      local cur = out[k]
      if deep and type(cur) == "table" and type(v) == "table"
          and not tbl.islist(cur) and not tbl.islist(v) then
        out[k] = extend(true, behavior, cur, v)
      elseif cur == nil or behavior == "force" then
        out[k] = (deep and type(v) == "table") and tbl.deepcopy(v) or v
      elseif behavior == "error" then
        error("key '" .. tostring(k) .. "' is in more than one table", 3)
      end -- "keep": the first value stays
    end
  end
  return out
end

-- behavior: "error" | "keep" | "force" decides what a key found twice does.
function tbl.extend(behavior, ...) return extend(false, behavior, ...) end
-- like extend, but tables found under the same key are merged recursively
-- (lists are values and replace each other).
function tbl.deep_extend(behavior, ...) return extend(true, behavior, ...) end

-- ---------------------------------------------------------------------------
-- winter.str
-- ---------------------------------------------------------------------------
local str = {}
winter.str = str

-- split(s, sep [, {plain=true, trimempty=false}]) -> list. sep is literal unless
-- plain = false, then a Lua pattern.
function str.split(s, sep, opts)
  opts = opts or {}
  local plain = opts.plain ~= false
  local out, pos = {}, 1
  if sep == "" then
    for i = 1, #s do out[i] = s:sub(i, i) end
    return out
  end
  while true do
    local a, b = s:find(sep, pos, plain)
    if not a then
      out[#out + 1] = s:sub(pos)
      break
    end
    out[#out + 1] = s:sub(pos, a - 1)
    pos = b + 1
  end
  if opts.trimempty then
    while out[1] == "" do table.remove(out, 1) end
    while out[#out] == "" do out[#out] = nil end
  end
  return out
end

function str.trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
function str.startswith(s, prefix) return s:sub(1, #prefix) == prefix end
function str.endswith(s, suffix) return suffix == "" or s:sub(-#suffix) == suffix end

-- quote for a POSIX shell
function str.shellescape(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end

-- ---------------------------------------------------------------------------
-- winter.inspect
-- ---------------------------------------------------------------------------
local function is_ident(k)
  return type(k) == "string" and k:match("^[%a_][%w_]*$") ~= nil
end

local function key_less(a, b)
  local ta, tb = type(a), type(b)
  if ta == tb and (ta == "number" or ta == "string") then return a < b end
  if ta == "number" then return true end
  if tb == "number" then return false end
  return tostring(a) < tostring(b)
end

-- winter.inspect(value [, {indent="  ", depth=math.huge, width=80}]) -> string
-- Keys are sorted; a table that fits `width` stays on one line; cycles show as
-- <cycle>.
function winter.inspect(value, opts)
  opts = opts or {}
  local indent = opts.indent or "  "
  local max_depth = opts.depth or math.huge
  local width = opts.width or 80
  local stack = {}

  local function render(v, level)
    local t = type(v)
    if t == "string" then return (string.format("%q", v):gsub("\\\n", "\\n")) end
    if t ~= "table" then return tostring(v) end
    if stack[v] then return "<cycle>" end
    if level >= max_depth then return "{...}" end
    stack[v] = true
    local parts, n = {}, #v
    for i = 1, n do parts[#parts + 1] = render(v[i], level + 1) end
    local keys = {}
    for k in pairs(v) do
      if not (type(k) == "number" and k >= 1 and k <= n and k % 1 == 0) then
        keys[#keys + 1] = k
      end
    end
    table.sort(keys, key_less)
    for _, k in ipairs(keys) do
      local ks = is_ident(k) and k or ("[" .. render(k, level + 1) .. "]")
      parts[#parts + 1] = ks .. " = " .. render(v[k], level + 1)
    end
    stack[v] = nil
    if #parts == 0 then return "{}" end
    local one = "{ " .. table.concat(parts, ", ") .. " }"
    if #one + level * #indent <= width and not one:find("\n", 1, true) then
      return one
    end
    local pad = string.rep(indent, level + 1)
    return "{\n" .. pad .. table.concat(parts, ",\n" .. pad) .. ",\n"
        .. string.rep(indent, level) .. "}"
  end

  return (render(value, 0))
end

-- ---------------------------------------------------------------------------
-- winter.validate
-- ---------------------------------------------------------------------------
-- winter.validate{ name = {value, "string"}, opt = {value, {"string","nil"}},
--                  fn = {value, function(v) return v > 0 end, "positive"} }
-- Raises "bad argument 'name': expected string, got number" at the caller.
function winter.validate(spec)
  local names = {}
  for name in pairs(spec) do names[#names + 1] = name end
  table.sort(names)
  for _, name in ipairs(names) do
    local item = spec[name]
    local value, check, extra = item[1], item[2], item[3]
    local ok, expected = true, nil
    if type(check) == "function" then
      ok = check(value) and true or false
      expected = type(extra) == "string" and extra or "a valid value"
    else
      local allowed = type(check) == "table" and check or { check }
      expected = table.concat(allowed, " or ")
      ok = false
      for _, ty in ipairs(allowed) do
        if ty == "callable" then
          local mt = getmetatable(value)
          ok = type(value) == "function" or (mt ~= nil and mt.__call ~= nil)
        else
          ok = (type(value) == ty)
        end
        if ok then break end
      end
      if not ok and extra == true and value == nil then ok = true end -- optional
    end
    if not ok then
      error(("bad argument '%s': expected %s, got %s"):format(
        name, expected, type(value)), 2)
    end
  end
end

-- ---------------------------------------------------------------------------
-- notify / schedule / defer / system
-- ---------------------------------------------------------------------------
winter.log = {
  levels = { TRACE = 0, DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4 },
  level = 2, -- messages below this are dropped
}

local level_prefix = { [3] = "warning: ", [4] = "error: " }

-- winter.notify(msg [, level=INFO]) shows msg in the status line.
function winter.notify(msg, level)
  level = level or winter.log.levels.INFO
  if level < winter.log.level then return end
  msg = tostring(msg):gsub("\n", " ")
  winter.ui.status((level_prefix[level] or "") .. msg)
end

-- run fn on the next loop iteration (safe from any callback)
function winter.schedule(fn) return winter.timer.after(1, fn) end

-- run fn after ms milliseconds; returns the timer id (winter.timer.cancel)
function winter.defer(fn, ms) return winter.timer.after(math.max(ms or 1, 1), fn) end

-- winter.system(cmd [, {stdin=, cwd=}], on_exit(res)) -> job id | nil
-- cmd is a string (run by the shell) or a list of arguments (quoted for you).
-- res = {code=, stdout=}: the output as winter.job.run delivers it.
function winter.system(cmd, opts, on_exit)
  if type(opts) == "function" then opts, on_exit = nil, opts end
  opts = opts or {}
  winter.validate {
    cmd = { cmd, { "string", "table" } },
    on_exit = { on_exit, "callable" },
  }
  local line = cmd
  if type(cmd) == "table" then
    local q = {}
    for i, a in ipairs(cmd) do q[i] = str.shellescape(a) end
    line = table.concat(q, " ")
  end
  if opts.cwd then line = "cd " .. str.shellescape(opts.cwd) .. " && " .. line end
  return winter.job.run(line, function(out, code)
    on_exit({ code = code, stdout = out })
  end, opts.stdin)
end

-- ---------------------------------------------------------------------------
-- variables: winter.g (global), winter.b[buf] (per buffer, by stable id)
-- ---------------------------------------------------------------------------
winter.g = winter.g or {}

local bvars = {}
winter.b = setmetatable({}, {
  __index = function(_, id)
    if type(id) ~= "number" then return nil end
    if not winter.api.buf_valid(id) then return nil end
    if id == 0 then id = winter.buf.id() end -- 0 is "current": key by the real id
    local v = bvars[id]
    if not v then
      for known in pairs(bvars) do -- drop what belonged to closed buffers
        if not winter.api.buf_valid(known) then bvars[known] = nil end
      end
      v = {}
      bvars[id] = v
    end
    return v
  end,
})

-- ---------------------------------------------------------------------------
-- winter.autocmd
-- ---------------------------------------------------------------------------
-- Names people know from nvim map to winter's own; winter's work as they are.
local event_alias = {
  BufEnter = "buf_enter", BufWritePre = "buf_write_pre",
  BufWritePost = "buf_write_post", BufDelete = "buf_close",
  BufUnload = "buf_close", BufWipeout = "buf_close", TextChanged = "text_changed",
  CursorMoved = "cursor_moved", ModeChanged = "mode_changed",
  FileType = "file_type", VimEnter = "ready", VimLeavePre = "quit",
  VimLeave = "quit", InsertEnter = "insert_enter", InsertLeave = "insert_leave",
  VisualEnter = "visual_enter", VisualLeave = "visual_leave",
  WinEnter = "win_enter", WinLeave = "win_leave", WinNew = "win_new",
  WinClosed = "win_close",
  BufNew = "buf_new", BufAdd = "buf_new", TextYankPost = "yank_post",
  LspAttach = "lsp_attach", LspDetach = "lsp_detach",
}

-- events winter does not emit itself: derived from mode_changed
local derived = {
  insert_enter = function(e) return e.new == "insert" end,
  insert_leave = function(e) return e.old == "insert" end,
  visual_enter = function(e) return e.new == "visual" end,
  visual_leave = function(e) return e.old == "visual" end,
}

local argless = { ready = true, quit = true, text_changed = true,
  cursor_moved = true, reload = true }

local acmds = {}   -- id -> record
local groups = {}  -- name -> { [id] = true }
local next_ac = 1

local autocmd = {}
winter.autocmd = autocmd

local function drop(id)
  local r = acmds[id]
  if not r then return false end
  for _, nid in ipairs(r.native) do winter.ev.off(nid) end
  acmds[id] = nil
  if r.group and groups[r.group] then groups[r.group][id] = nil end
  return true
end

-- autocmd.group(name [, {clear=true}]) -> name. Existing groups are emptied
-- unless clear = false, so re-running a script does not stack handlers.
function autocmd.group(name, opts)
  winter.validate { name = { name, "string" } }
  groups[name] = groups[name] or {}
  if not opts or opts.clear ~= false then
    for id in pairs(groups[name]) do drop(id) end
  end
  return name
end

-- autocmd.create(events, {callback=fn(ev), group=, pattern=, once=, buffer=,
--                         desc=}) -> id
-- events: a name or list. pattern: file globs (buf_* events) or languages
-- (file_type). buffer: a buffer id (or true for the current one): only events
-- about that buffer fire. callback returns true to remove itself.
function autocmd.create(events, opts)
  winter.validate {
    events = { events, { "string", "table" } },
    opts = { opts, "table" },
  }
  winter.validate { callback = { opts.callback, "callable" } }
  local list = type(events) == "table" and events or { events }
  local id = next_ac
  local want_buf = opts.buffer
  if want_buf == true then want_buf = winter.buf.id() end
  local rec = { id = id, native = {}, events = {}, group = opts.group,
    pattern = opts.pattern, desc = opts.desc, once = opts.once and true or false,
    buffer = want_buf }

  for _, name in ipairs(list) do
    local ev = event_alias[name] or name
    local base, pred = ev, derived[ev]
    if pred then base = "mode_changed" end
    if opts.pattern and (pred or argless[ev]) then
      error("event '" .. name .. "' has no pattern to match", 2)
    end
    rec.events[#rec.events + 1] = ev
    local nid = winter.ev.on(base, function(e)
      if pred and not pred(e) then return end
      if want_buf and e.buf ~= want_buf then return end
      e.event, e.id, e.group = ev, id, rec.group
      if rec.once then drop(id) end
      if opts.callback(e) == true then drop(id) end
    end, { pattern = opts.pattern })
    rec.native[#rec.native + 1] = nid
  end

  next_ac = next_ac + 1
  acmds[id] = rec
  if rec.group then
    groups[rec.group] = groups[rec.group] or {}
    groups[rec.group][id] = true
  end
  return id
end

-- autocmd.del(id) -> bool
function autocmd.del(id) return drop(id) end

-- autocmd.clear{group=, event=, buffer=}: removes every match (no filter: all)
function autocmd.clear(opts)
  opts = opts or {}
  local ev = opts.event and (event_alias[opts.event] or opts.event)
  local ids = {}
  for id, r in pairs(acmds) do
    if (not opts.group or r.group == opts.group)
        and (not ev or tbl.contains(r.events, ev))
        and (not opts.buffer or r.buffer == opts.buffer) then
      ids[#ids + 1] = id
    end
  end
  for _, id in ipairs(ids) do drop(id) end
  return #ids
end

-- autocmd.list{group=, event=} -> { {id, events, group, pattern, desc, once,
--                                   buffer}, ... } sorted by id
function autocmd.list(opts)
  opts = opts or {}
  local ev = opts.event and (event_alias[opts.event] or opts.event)
  local out = {}
  for _, r in pairs(acmds) do
    if (not opts.group or r.group == opts.group)
        and (not ev or tbl.contains(r.events, ev)) then
      out[#out + 1] = { id = r.id, events = tbl.slice(r.events), group = r.group,
        pattern = r.pattern, desc = r.desc, once = r.once, buffer = r.buffer }
    end
  end
  table.sort(out, function(a, b) return a.id < b.id end)
  return out
end

-- ---------------------------------------------------------------------------
-- buffer handles: winter.buf.get(id) -> object
-- ---------------------------------------------------------------------------
-- A handle holds the stable id, never an index, so it stays correct while
-- other buffers open and close. Every method raises "buffer closed" once the
-- buffer is gone; test with :is_valid().
local api = winter.api
local Buf = {}
Buf.__index = Buf
local handles = setmetatable({}, { __mode = "v" })

local function live(self)
  if not api.buf_valid(self.id) then error("buffer " .. self.id .. " is closed", 3) end
  return self.id
end

-- winter.buf.get([id]) -> handle | nil   (no id: the current buffer)
function winter.buf.get(id)
  id = id or winter.buf.id()
  if not id or not api.buf_valid(id) then return nil end
  local h = handles[id]
  if not h then
    h = setmetatable({ id = id }, Buf)
    handles[id] = h
  end
  return h
end

-- the buffers as handles, in buffer-list order
function winter.buf.handles()
  local out = {}
  for _, info in ipairs(winter.buf.list()) do out[#out + 1] = winter.buf.get(info.id) end
  return out
end

Buf.__tostring = function(self) return "buf(" .. self.id .. ")" end

function Buf:is_valid() return api.buf_valid(self.id) end
function Buf:index() return winter.buf.by_id(live(self)) end
function Buf:info() return winter.buf.info(self:index()) end
function Buf:name() return api.buf_get_name(live(self)) end
function Buf:lang() return self:info().lang end
function Buf:modified() return self:info().modified end
function Buf:line_count() return api.buf_line_count(live(self)) end
function Buf:tick() return api.buf_changedtick(live(self)) end

-- lines first..last (1-based, inclusive; last < 0 counts from the end)
function Buf:lines(first, last) return api.buf_get_lines(live(self), first, last) end
function Buf:line(n) return api.buf_get_lines(live(self), n, n)[1] end
function Buf:set_lines(first, last, lines) api.buf_set_lines(live(self), first, last, lines) end
-- add lines after the last one
function Buf:append(lines)
  local n = self:line_count()
  api.buf_set_lines(live(self), n + 1, n, lines)
end
function Buf:text(l1, c1, l2, c2) return api.buf_get_text(live(self), l1, c1, l2, c2) end
function Buf:set_text(l1, c1, l2, c2, lines)
  api.buf_set_text(live(self), l1, c1, l2, c2, lines)
end
function Buf:cursor() return api.buf_get_cursor(live(self)) end
function Buf:set_cursor(line, col) api.buf_set_cursor(live(self), line, col) end

-- Buf:attach{on_edit=fn(ev), on_close=fn(buf)} -> handle; see winter.api.buf_attach
function Buf:attach(opts) return api.buf_attach(live(self), opts) end
function Buf:detach(handle) return api.buf_detach(handle) end

function Buf:switch() return winter.buf.switch(self:index()) end
function Buf:close(force) return winter.buf.close(self:index(), force) end

-- syntax tree (winter.ts) of this buffer
function Buf:ts_root() return winter.ts.root(live(self)) end
function Buf:ts_node_at(line, col, opts) return winter.ts.node_at(live(self), line, col, opts) end
-- compile a query for this buffer's language
function Buf:ts_query(source)
  local lang = winter.ts.lang(live(self))
  if not lang then return nil, "this buffer has no syntax tree" end
  return winter.ts.query(lang, source)
end

-- buffer-local keymaps (winter.keymap.set with buffer = this id)
function Buf:map(modes, keys, rhs, opts)
  return winter.keymap.set(modes, keys, rhs, tbl.extend("force", opts or {}, { buffer = live(self) }))
end
function Buf:unmap(modes, keys) return winter.keymap.del(modes, keys, { buffer = live(self) }) end
function Buf:keymaps(mode) return winter.keymap.list({ buffer = live(self), mode = mode }) end

-- buffer-local variables
function Buf:var(name) return winter.b[live(self)][name] end
function Buf:set_var(name, value) winter.b[live(self)][name] = value end


-- ---------------------------------------------------------------------------
-- winter.ts.captures
-- ---------------------------------------------------------------------------
local tsq_cache = {}  -- language .. query text -> compiled query

-- winter.ts.captures(buf, source [, {first=, last=, limit=}]) -> captures | nil, err
-- Runs a query on a buffer's tree (buf 0 = current). Compiled queries are
-- cached, so calling this on every event is cheap.
function winter.ts.captures(buf, source, opts)
  winter.validate { buf = { buf, "number" }, source = { source, "string" } }
  local root, err = winter.ts.root(buf)
  if not root then return nil, err end
  local lang = root:lang()
  local key = lang .. "\0" .. source
  local q = tsq_cache[key]
  if not q then
    q, err = winter.ts.query(lang, source)
    if not q then return nil, err end
    tsq_cache[key] = q
  end
  return q:captures(root, opts)
end

-- ---------------------------------------------------------------------------
-- winter.keymap.ft
-- ---------------------------------------------------------------------------
-- winter.keymap.ft(langs, modes, keys, rhs [, opts]): a buffer-local map in
-- every buffer of those languages, the ones open now and the ones opened (or
-- re-detected) later: the ftplugin pattern. langs: "lua" | {"lua", "nix"}.
-- opts as winter.keymap.set (desc, wants_char); `buffer` is set for you.
function winter.keymap.ft(langs, modes, keys, rhs, opts)
  winter.validate {
    langs = { langs, { "string", "table" } },
    modes = { modes, { "string", "table" } },
    keys = { keys, "string" },
    rhs = { rhs, { "string", "function" } },
  }
  local list = type(langs) == "table" and langs or { langs }
  local function apply(id)
    winter.keymap.set(modes, keys, rhs, tbl.extend("force", opts or {}, { buffer = id }))
  end
  for _, info in ipairs(winter.buf.list()) do
    if tbl.contains(list, info.lang) then apply(info.id) end
  end
  return winter.autocmd.create("file_type", {
    group = "winter.keymap.ft", pattern = list, desc = "keymap.ft " .. keys,
    callback = function(ev) apply(ev.buf) end,
  })
end

-- ---------------------------------------------------------------------------
-- window handles: winter.win.get(id) -> object
-- ---------------------------------------------------------------------------
-- Like buffer handles: bound to the window id (stable for the window's life),
-- methods raise "window N is closed" afterwards; test with :is_valid().
local wapi = winter.win
local Win = {}
Win.__index = Win
local whandles = setmetatable({}, { __mode = "v" })

local function wlive(self)
  if not wapi.info(self.id) then error("window " .. self.id .. " is closed", 3) end
  return self.id
end

-- winter.win.get([id]) -> handle | nil   (no id: the focused window)
function wapi.get(id)
  id = id or wapi.current()
  if not wapi.info(id) then return nil end
  local h = whandles[id]
  if not h then
    h = setmetatable({ id = id }, Win)
    whandles[id] = h
  end
  return h
end

-- every window as a handle, in layout order
function wapi.handles()
  local out = {}
  for _, id in ipairs(wapi.list()) do out[#out + 1] = wapi.get(id) end
  return out
end

Win.__tostring = function(self) return "win(" .. self.id .. ")" end

function Win:is_valid() return wapi.info(self.id) ~= nil end
function Win:info() return wapi.info(wlive(self)) end
function Win:is_focused() return self:info().focused end
function Win:focus() return wapi.focus(wlive(self)) end
-- the buffer shown, as a buffer handle
function Win:buf() return winter.buf.get(wapi.get_buf(wlive(self))) end
-- show a buffer (handle or id); false if it does not exist
function Win:set_buf(buf)
  return wapi.set_buf(wlive(self), type(buf) == "table" and buf.id or buf)
end
function Win:cursor() return wapi.get_cursor(wlive(self)) end
function Win:set_cursor(line, col) wapi.set_cursor(wlive(self), line, col) end
function Win:top() return self:info().top end
function Win:set_top(line) wapi.set_top(wlive(self), line) end
-- text columns, text rows
function Win:size() local i = self:info(); return i.width, i.height end
function Win:close(force) return wapi.close(wlive(self), force) end
-- split this window; the new window (a handle) takes focus
function Win:split(opts)
  wapi.focus(wlive(self))
  local id = wapi.split(opts)
  return id and wapi.get(id) or nil
end

-- ---------------------------------------------------------------------------
-- reload: the native side already dropped every ev subscription
-- ---------------------------------------------------------------------------
function winter._reset()
  tsq_cache = {}
  acmds, groups, next_ac = {}, {}, 1
end
)LUA";

}  // namespace

void script_open_runtime() {
    lua_State* L = script_state();
    if(!L) return;
    if(luaL_loadbuffer(L, kRuntime, std::strlen(kRuntime), "=winter/runtime") !=
       LUA_OK) {
        lua_pop(L, 1);  // can't happen: the text is checked at build time
        return;
    }
    script_call(0);
}

// Config reload: the native layer has already dropped every Lua handler and
// buffer attachment; this clears what the runtime remembers about them.
void script_runtime_reset() {
    script_api_reset();
    lua_State* L = script_state();
    if(!L) return;
    lua_getglobal(L, "winter");
    lua_getfield(L, -1, "_reset");
    if(lua_isfunction(L, -1)) script_call(0);
    else lua_pop(L, 1);
    lua_pop(L, 1);
}
