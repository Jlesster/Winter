# Winter Lua API

Everything lives under the global `winter`. Config is `~/.config/winter/init.lua`
(`$XDG_CONFIG_HOME/winter`); with the `hot_reload` option on it is reloaded when a
`.lua` file below that directory changes.

## Writing a plugin

```
~/.config/winter/plugins/<name>/plugin.lua      runs at startup, after init.lua
~/.config/winter/plugins/<name>/init.lua        library: require("<name>")
```

`plugin.lua` receives `(name, path)` as `...`. A plugin that errors is skipped
and reported; the others still load. `winter.plugin.list()` shows what ran and
`winter.plugin.disable("name")` (from `init.lua`) turns one off.

**Reload is clean.** On a config reload winter removes every event handler,
command, keymap, parser and buffer attachment a script created, then re-runs
`init.lua` and the plugins. Plugins must therefore be re-runnable: create
autocmds inside a group (`autocmd.group` empties it first) and you never stack
handlers.

```lua
-- plugins/trim_ws/plugin.lua
local name, path = ...
local group = winter.autocmd.group("trim_ws")
winter.autocmd.create("BufWritePre", {
  group = group,
  desc = "strip trailing whitespace on save",
  callback = function(ev)
    local buf = winter.buf.get(ev.buf)
    if not buf or buf:var("trim_ws_off") then return end
    for i, line in ipairs(buf:lines()) do
      local trimmed = line:gsub("%s+$", "")
      if trimmed ~= line then buf:set_lines(i, i, { trimmed }) end
    end
  end,
})
```

Conventions: lines and columns are **1-based**, line ranges are **inclusive**,
columns are **byte** columns. Functions that can fail for ordinary reasons
(files, JSON, shell) return `nil, err` instead of raising.

## Buffers: stable ids

`winter.buf.*` addresses buffers by **index**, which shifts whenever a buffer
closes. Anything you keep across events should be the **id**: it belongs to the
buffer for life and is never reused in a session.

| | |
|---|---|
| `winter.buf.info(i).id`, `winter.buf.list()[i].id` | the id of buffer `i` |
| `winter.buf.id([index])` | id of a buffer (default: current) |
| `winter.buf.by_id(id)` | index for an id, `nil` if it closed |
| every event table | `ev.buf` = id of the buffer the event is about |

### Explicit-buffer API: `winter.api`

Works on **any** buffer, current or not. `buf` is an id; `0` means current.

| function | |
|---|---|
| `buf_valid(buf)` | bool |
| `buf_line_count(buf)` | n |
| `buf_get_lines(buf [, first=1 [, last=-1]])` | list; negative `last` counts from the end |
| `buf_set_lines(buf, first, last, lines)` | replace `first..last`; `last == first-1` inserts before `first`; `{}` deletes. One undo step |
| `buf_get_text(buf, l1, c1, l2, c2)` | text in `[start, end)` |
| `buf_set_text(buf, l1, c1, l2, c2, lines)` | replace that range |
| `buf_get_name(buf)`, `buf_changedtick(buf)` | |
| `buf_get_cursor(buf)`, `buf_set_cursor(buf, line, col)` | for a background buffer: the cursor it will have when entered |
| `buf_attach(buf, {on_edit=, on_close=})` → handle | change feed, below |
| `buf_detach(handle)` | bool |

#### Change feed

```lua
local h = winter.api.buf_attach(buf, {
  on_edit = function(e) ... end,   -- return true to detach
  on_close = function(buf) ... end,
})
```

`e` = `{buf, tick, origin, offset, old_len, new_len, first_line, first_col,
old_last_line, old_last_col, new_last_line, new_last_col}`. `offset` is the
0-based byte offset; the `old_last_*` end is a position in the text *before* the
edit and `new_last_*` *after* it (both exclusive). `origin` is `"user"`,
`"undo"`, `"redo"`, `"snippet"`, `"lsp"` or `"external"`.

- It fires once per primitive edit, right after the edit finished, so one
  operator can produce several callbacks and the buffer may be mid-operation.
- Editing the attached buffer from `on_edit` triggers `on_edit` again: guard
  with a flag or `e.origin`.
- Attachments end when the buffer closes (`on_close`) and on config reload.

### Buffer handles

`winter.buf.get([id])` returns an object bound to the id (or `nil`). Methods
raise `buffer N is closed` once the buffer is gone; test with `:is_valid()`.

```lua
local b = winter.buf.get()          -- current buffer
b:name()  b:lang()  b:modified()  b:line_count()  b:tick()  b:index()  b:info()
b:lines([first [, last]])   b:line(n)   b:set_lines(first, last, lines)   b:append(lines)
b:text(l1,c1,l2,c2)         b:set_text(l1,c1,l2,c2, lines)
b:cursor()  b:set_cursor(line, col)
b:attach{on_edit=..., on_close=...}   b:detach(handle)
b:switch()  b:close([force])
b:var(name)  b:set_var(name, value)         -- == winter.b[b.id][name]
winter.buf.handles()                         -- every buffer, in list order
```

### Variables

`winter.g` is a plain global table. `winter.b[id]` (id `0` = current) is a table
per buffer, or `nil` for an invalid id; it is dropped once the buffer is gone.

## Events and autocmds

`winter.ev.on/off/emit` is the raw layer. Native events: `buf_enter`,
`buf_write_pre`, `buf_write_post`, **`buf_close`**, `file_type`, `mode_changed`,
`text_changed`, `cursor_moved`, `key`, `win_new`, `win_close`, `win_enter`,
`win_leave`, **`buf_new`**, **`yank_post`**, **`lsp_attach`**, **`lsp_detach`**,
`ready`, `reload`, `quit`. Payload: `event`, `buf`, plus named fields
(`filename`, `lang`, `old`, `new`, `key`, ...). `buf_close` fires before the buffer
goes away, so `winter.api` and handles still work in it.

| event | fields | fires |
|---|---|---|
| `buf_new` | `filename` | a buffer joined the list (file opened, scratch buffer created), already loaded, not yet entered |
| `yank_post` | `regname` (`""` = default), `kind` (`"char"`/`"line"`), `first_line`, `first_col`, `last_line`, `last_col` | after a yank; the range is 1-based with an exclusive end column. The text is in the register |
| `lsp_attach` | `server` | a language server took the buffer (one loop turn later, so handlers may open buffers) |
| `lsp_detach` | `server` | the buffer closed (right then), or its server died (one loop turn later) |

Line and column fields, like `win`, arrive as numbers; the others are strings. nvim
names `BufNew`/`BufAdd`, `TextYankPost`, `LspAttach` and `LspDetach` work in
`winter.autocmd`.

```lua
-- report what was yanked; the range is enough to build a highlight-on-yank with winter.deco
winter.autocmd.create("TextYankPost", { callback = function(ev)
  -- ev.first_line .. ev.last_line (and the columns) are the yanked range
  winter.notify(("yanked %d line(s) into %q"):format(ev.last_line - ev.first_line + 1, ev.regname))
end })
```

`key` reports every **typed** key, before it is handled, as notation in
`ev.key` (`"j"`, `"<C-r>"`, `"<Esc>"`); `pattern` is a glob on that text
(`pattern = "<C-*>"`). It only observes: a handler cannot swallow or change the
key. Keys sent by `winter.normal` and by dot-repeat are not reported, so a handler
may send keys without triggering itself. It costs nothing while nobody listens.

`winter.autocmd` adds groups, nvim names and cleanup:

```lua
winter.autocmd.group(name [, {clear=true}]) -> name
winter.autocmd.create(events, {callback=fn(ev), group=, pattern=, once=, buffer=, desc=}) -> id
winter.autocmd.del(id)                      winter.autocmd.clear{group=, event=, buffer=}
winter.autocmd.list{group=, event=}
```

- `events`: a name or a list. nvim names work (`BufEnter`, `BufWritePre`,
  `BufWritePost`, `BufDelete`/`BufUnload`/`BufWipeout`, `TextChanged`,
  `CursorMoved`, `ModeChanged`, `FileType`, `VimEnter`, `VimLeavePre`).
- `InsertEnter`, `InsertLeave`, `VisualEnter`, `VisualLeave` (or
  `insert_enter` ...) are derived from `mode_changed`.
- `pattern`: file globs for `buf_*` events, languages for `file_type`; an error
  for events that have nothing to match.
- `buffer`: an id, or `true` for the current buffer.
- The callback gets `ev` with `event`, `id`, `group`, `buf`. Returning `true`
  removes the autocmd.

## Registers

Editor behaviour (vim's rules): `"ayy` yanks into `a`, `"Ayy` appends to it,
`"ap` pastes it, `"_dd` deletes without touching anything, `"+y` / `"+p` use the
system clipboard, and in insert mode `<C-r>a` inserts register `a`.

| register | holds |
|---|---|
| `"` | the unnamed register: what plain `y`/`d`/`c`/`p` use; it follows the system clipboard |
| `0` | the last yank |
| `1`-`9` | the last deletes that spanned lines (newest in `1`, older ones shift up) |
| `-` | the last delete within one line |
| `a`-`z` / `A`-`Z` | named; the uppercase name appends. A named register leaves the clipboard alone |
| `_` | black hole: nothing is stored, nothing changes |
| `+` `*` | the system clipboard |
| `%` | the current file name (read only) |
| `.` | the text typed in the last insert session (read only) |
| `:` | the last command line (read only) |
| `/` | the last search pattern (read only) |

`".p`, `":p` and `"/p` paste them, `<C-r>.`, `<C-r>:` and `<C-r>/` insert them, and
they cannot be written (`reg.set` returns false). An insert session that typed
nothing leaves `.` as it was. `.` records what you type, paste and `<C-r>`-insert,
with backspace, `<C-w>` and `<C-u>` applied; it does not include completion or
snippet expansion text, and a snippet-trigger `<Tab>` counts as a tab. With several
cursors the text is recorded once.

Differences from vim: the delete-with-motion exceptions (`d%`, `d/`, ...) that
force register `1` are not implemented.
Dot-repeat remembers the register (`"ap` then `.` repeats `"ap`).

```lua
winter.reg.get([name='"']) -> text, linewise | nil    -- nil when empty or unknown
winter.reg.set(name, text [, linewise]) -> bool        -- "A" appends; false for read-only/unknown
winter.reg.list()                                      -- names that hold something
```

`reg.set` does not change `"` (what `p` pastes), except for `'"'`, `'+'` and
`'*'`, which also update the system clipboard.

## Windows

A window is addressed by its **id**, stable for the window's life (`0` means the
focused one). `winter.win.get(id)` returns a handle; its methods raise
`window N is closed` afterwards, test with `:is_valid()`.

```lua
local w = winter.win.get()          -- focused window; winter.win.handles() lists all
w:info()    -- {id, buf, focused, x, y, width, height, line, col, top, left}
w:buf()  w:set_buf(buf_or_id)  w:cursor()  w:set_cursor(line, col)
w:top()  w:set_top(line)  w:size()  w:focus()  w:close([force])
local nw = w:split{vertical=true, before=true}     -- handle of the new, focused window
```

`buf` in `info` is a stable buffer id (`winter.buf.get(w.buf)`); `x`/`y` are
0-based screen cells; `width`/`height` are the text area (height excludes the
status row); lines and columns are 1-based. A window that is not focused keeps its
own cursor and scroll, and `set_buf` on it gives it that buffer's remembered view.
`set_top` also pulls the cursor into view (keeping `scrolloff`), otherwise the
next frame would scroll straight back; wrapped lines and closed folds are not
accounted for.

Natives under `winter.win` (the handles wrap these; the older `size()` and `top()`
still describe the focused window):

```lua
current()  list()  info(id)  showing(buf)
get_cursor(id)  set_cursor(id, line [, col])  get_buf(id)  set_buf(id, buf)  set_top(id, line)
focus(id)  split([{vertical=, before=}])  close(id [, force]) -> true | false, err  only()
cmd(key [, count])      -- any CTRL-W command: "x" swap, "r" rotate, "H" move far left, "=" ...
neighbor("left"|"down"|"up"|"right") -> id | nil
resize(dw, dh)  equalize()  maximize("width"|"height")      -- the focused window
```

`close` refuses the last window, and (without `force`) a window holding the only
view of a modified buffer. Events: `win_new`, `win_close` (fires while the window
still exists), `win_enter`, `win_leave`, each with `ev.win` = the window id (a
number); nvim names `WinNew`, `WinClosed`, `WinEnter`, `WinLeave` work in
`winter.autocmd`. Keys, commands and Lua all produce them.

## Syntax trees

```lua
winter.ts.root([buf]) -> node | nil, err                     -- buf 0 / omitted = current
winter.ts.node_at(buf, line, col [, {named=false}]) -> node | nil, err
winter.ts.lang([buf]) -> string | nil                       -- nil when there is no tree
winter.ts.query(lang, source) -> query | nil, err           -- err = "3:14: unknown node type near 'foo'"
winter.ts.captures(buf, source [, opts]) -> captures | nil, err   -- compile (cached) + run on the buffer
buf:ts_root()   buf:ts_node_at(line, col, opts)   buf:ts_query(source)
```

The tree is brought up to date with the text when you ask for it (an incremental
reparse), so you always see the buffer as it is now. Only the main tree is
exposed: injected languages (code blocks in markdown, ...) are not.

**Nodes** (1-based lines and columns; a range's end column is exclusive):

```lua
n:type()  n:is_named()  n:is_missing()  n:is_extra()  n:is_error()  n:has_error()
n:range() -> line1, col1, line2, col2      n:byte_range() -> start, end   (0-based bytes)
n:text()  n:sexpr()  n:id()  n:buf()  n:lang()  n:equals(other)         -- also: a == b
n:parent()  n:next_sibling()  n:prev_sibling()  n:next_named_sibling()  n:prev_named_sibling()
n:child_count()  n:child(i)  n:children()       n:named_child_count()  n:named_child(i)  n:named_children()
n:field("name") -> list of the children with that field name
n:descendant(l1, c1 [, l2, c2])  n:named_descendant(...)   -- smallest node covering the range
```

**A node goes stale when its buffer changes.** Any method then raises `stale node`
instead of reading moved memory; fetch the node again (`tostring` never raises
and shows `node(stale)`). `n:id()` is only comparable within one unedited tree.

**Queries** use tree-sitter's query language. Predicates `#eq?` `#not-eq?`
`#match?` `#not-match?` `#any-of?` `#not-any-of?` are evaluated (regex syntax is
ECMAScript); directives such as `#set!` are accepted and ignored; any other
predicate (`#lua-match?`, ...) is a compile error rather than silently true.

```lua
q:captures(node [, {first=, last=, limit=}]) -> { {name=, node=, pattern=}, ... }   -- document order
q:matches(node [, opts]) -> { {pattern=, captures={ {name=, node=}, ... }, named={ name = node }}, ... }
q:capture_names()   q:pattern_count()   q:lang()
```

`first` / `last` are 1-based inclusive lines; `pattern` is 1-based; `named` holds
the first node of each capture name. A query only runs on nodes of its own language.

```lua
-- every function name in a C buffer
local caps = winter.ts.captures(0, "(function_definition declarator: (function_declarator declarator: (identifier) @name))")
for _, c in ipairs(caps) do print(c.node:text(), (c.node:range())) end
```

## Keymaps

```lua
winter.keymap.set(modes, keys, rhs [, opts])   -- rhs: an action name, or function(ctx) with ctx = {count=, arg=}
winter.keymap.del(modes, keys [, {buffer=}])   -- -> true if something was mapped
winter.keymap.list([{mode=, buffer=}])         -- -> { {mode, keys, action, desc, buffer}, ... }
```

`modes` is letters from `n i r v x c s f t`, a word (`"normal"`), or a table of
those. `opts`: `wants_char = true` (the next typed character arrives as
`ctx.arg`), `desc = "text"` (kept for `keymap.list`, e.g. for a which-key picker),
and `buffer`.

**Buffer-local maps** (`buffer = true` for the current buffer, or a buffer id):

```lua
winter.keymap.set("n", "gX", function() ... end, { buffer = true, desc = "run this file" })
local b = winter.buf.get()
b:map("n", "<C-j>", "some.action", { desc = "..." })   -- same, bound to the handle's id
b:unmap("n", "<C-j>")    b:keymaps("n")

-- the ftplugin pattern: every buffer of these languages, open now or opened later
winter.keymap.ft("markdown", "n", "<C-b>", function() ... end, { desc = "bold" })
```

A buffer-local map shadows the global one for the same keys in that buffer only
(`keymap.del` with `buffer` makes the global map show through again). Chords
combine across both layers: a global `gxx` still works next to a local `gx`, and
the local `gx` waits for the second key. Local maps exist for modes `n i r v`
only; the command line, files tree and terminal ignore them, so asking for those
modes with `buffer` is an error. They go away with the buffer and on config
reload. Not implemented: `expr`, `nowait` and `silent`.

`keymap.list` without `buffer` lists the global maps, built-ins included (`desc`
only exists for maps made with it).

## Sending keys

```lua
winter.normal(keys) -> true | nil, err      -- run now, like :normal
winter.feedkeys(keys) -> true | nil, err    -- queue for the next loop turn, as if typed
```

`keys` is key notation: plain characters plus `<Esc> <CR> <Tab> <BS> <C-w> <A-j>
<C-Left> ...`, with `<lt>` for a literal `<`. A bad combination such as `<C-S-a>`
returns `nil, err`.

`normal` runs the keys immediately, in the current mode, with your keymaps in
effect (`:normal`, not `:normal!`). Anything the keys leave unfinished, or a mode
they started (insert, a command line, a pending operator), is ended as if `<Esc>`
was typed, so `winter.normal("ihello")` leaves you in normal mode. A caller that
was already in insert mode stays in it. The keys are **not** remembered for `.`:
`.` repeats whatever key ran your script. Injections nest at most 16 deep.

`feedkeys` returns at once; the keys run on the next turn of the event loop and
count as typed (`.` remembers them, a half-typed command stays pending).

```lua
winter.keymap.set("n", "gX", function() winter.normal('"_dd') end)   -- delete a line, keep the register
winter.autocmd.create("key", { pattern = "<C-*>", callback = function(ev) winter.notify(ev.key) end })
```

## Marks

`ma` sets mark `a` at the cursor; `'a` jumps to its line, `` `a `` to its exact
spot. They also work as operator motions (`d'a`, `` y`a ``), in visual mode, and
in ex ranges (`:'a,'bd`, `:'a,.s/x/y/`).

| mark | meaning |
|---|---|
| `a`-`z` | this buffer. Follows the text as it is edited |
| `A`-`Z` | file marks: jump from any buffer; reopen the file if its buffer was closed |
| `'` `` ` `` | where the latest jump started, so `''` swaps back and forth. Every "go somewhere else" feature (LSP goto, pickers, `'a`) sets it |
| `.` | where the buffer was last changed (read only) |
| `<` `>` | start and end of the last visual selection (also settable) |

A mark inside deleted text slides to where the deletion began (vim deletes it).
Marks do not survive the buffer being reloaded from disk. Not implemented: the
`[ ] ^ "` and `0-9` marks, and the `:marks` / `:delmarks` commands (use Lua).

```lua
winter.mark.set(name [, {buf=, line=, col=}]) -> bool  -- default: the cursor; line/col 1-based
winter.mark.get(name [, buf]) -> {name, buf, file, line, col} | nil   -- buf == 0: buffer closed, open `file`
winter.mark.del(name [, buf])        winter.mark.list([buf])
winter.mark.jump(name [, {exact=false}]) -> bool
```

## Files, shell, JSON

```lua
winter.fs.stdpath("config"|"data"|"cache"|"state")
winter.fs.exists(p) is_dir(p) is_file(p) executable(p)
winter.fs.stat(p)            -- {type="file"|"directory"|"link"|"other", size, mtime, executable} | nil, err
winter.fs.read(p)            winter.fs.write(p, data [, {append=, parents=}])
winter.fs.readdir(p)         -- sorted {name, type} list
winter.fs.mkdir(p [, {parents=}])   winter.fs.remove(p [, {recursive=}])   winter.fs.rename(a, b)
winter.fs.realpath(p)  cwd()  joinpath(...)  dirname(p)  basename(p)  normalize(p)
winter.fs.find(names [, {path=, upward=, type=, limit=, stop=}])   -- names/globs; downward skips .git, node_modules
winter.fs.root({".git", "Cargo.toml"} [, path])                  -- nearest ancestor holding a marker
```

```lua
winter.system(cmd, [{stdin=, cwd=}], function(res) end)   -- async; cmd = string or arg list (quoted for you)
winter.shell(cmd [, stdin]) -> output, code               -- BLOCKS the editor: short commands only
winter.json.encode(v) / decode(text)                      -- nil, err on failure; JSON null decodes to nil
```

## Utilities

`winter.tbl` (`islist isempty count keys values contains map filter slice
deepcopy extend deep_extend`), `winter.str` (`split trim startswith endswith
shellescape`), `winter.inspect(v [, {depth=, width=, indent=}])`,
`winter.validate{name = {value, "type"|{types}|fn [, msg|optional]}}`,
`winter.notify(msg [, level])` with `winter.log.levels` / `winter.log.level`,
`winter.schedule(fn)`, `winter.defer(fn, ms)`.

## Already available (see the sources for signatures)

`winter.opt` / `opt_local` · `winter.buf` (`line line_count lines set_lines text
insert name lang replace list info current switch open close cycle alt save
save_all modified`) · `winter.cursor` · `winter.mode()` / `selection()` ·
`winter.keymap.set/del` · `winter.action.add/run/list` · `winter.cmd.add/run` ·
`winter.ext.operator/motion/object` · `winter.ev` · `winter.job` ·
`winter.timer` · `winter.ui` (`input select status`) · `winter.picker` ·
`winter.float` · `winter.deco` (spans / eol text in namespaces) · `winter.diag` ·
`winter.lsp` · `winter.fold` · `winter.snippet` · `winter.statusline` ·
`winter.term` · `winter.theme` · `winter.win` (`size top`) · `winter.jump` ·
`winter.errparser` · `winter.options` · `winter.format` · `winter.lang.comment` ·
`winter.tree.icons` · `winter.plugin`.

## Parity with vis and nvim

Legend: ✔ have · ＋ added in this batch · ✘ missing

| capability | vis | nvim | winter |
|---|---|---|---|
| commands, keymaps, operators/motions/text objects | `vis:command/map/*_new` | `create_user_command`, `keymap.set`, … | ✔ |
| options, buffer-local options | `vis.options`, `win.options` | `vim.o/bo` | ✔ (`opt`, `opt_local`) |
| read/edit any buffer | `file` objects | `nvim_buf_*` | ＋ `winter.api`, handles |
| stable buffer handle | file object | bufnr | ＋ ids |
| change notifications | – | `nvim_buf_attach` | ＋ `buf_attach` |
| autocmds with groups | `vis.events` | `nvim_create_autocmd` | ＋ `winter.autocmd` |
| buffer close event | `FILE_CLOSE` | `BufDelete` | ＋ `buf_close` |
| filesystem helpers | – | `vim.fs`, `vim.uv` | ＋ `winter.fs` (no watchers yet) |
| async processes | `vis:pipe` | `vim.system` | ✔ `job`, ＋ `system`, `shell` |
| JSON | – | `vim.json` | ＋ |
| tbl/str/inspect/validate | – | `vim.tbl_*` … | ＋ |
| buffer/global variables | – | `vim.b`, `vim.g` | ＋ (`winter.b`, `winter.g`) |
| notify, schedule, defer | `vis:info` | `vim.notify` … | ＋ |
| registers (named, numbered, clipboard, `.` `:` `/` `%`, `"x` prefix, insert `<C-r>`) | `vis.registers` | `getreg/setreg` | ＋ |
| marks (`m ' ` A-Z . < >`, ex ranges) | `file:mark_*` | `nvim_buf_set_mark` | ＋ `winter.mark` |
| inject keys (`:normal`, `feedkeys`) | `vis:feedkeys` | `nvim_feedkeys` | ＋ `winter.normal`, `winter.feedkeys` (`:normal!` ignoring maps: not yet) |
| key-input hook | `INPUT` event | `vim.on_key` | ＋ `key` event (observe only) |
| windows API (list/split/close/focus, per-window cursor, buffer, scroll) | `vis.win` | `nvim_win_*` | ＋ `winter.win` handles (no per-window options, no exact resize) |
| **selections / multi-cursor API** | `win.selections` | – | ✘ (multicursor is C++ only) |
| buffer-local keymaps, `desc`, ftplugin-style maps | `vis:map(win)` | `keymap.set{buffer=}` | ＋ (`expr`, `nowait`, `silent` not yet) |
| **moving marks with text** (extmarks) | – | `nvim_buf_set_extmark` | ～ `deco` spans are anchored; no query API |
| tree-sitter nodes and queries | – | `vim.treesitter` | ＋ `winter.ts` (main tree only; no `#lua-match?`; no injections) |
| diagnostics write | – | `vim.diagnostic.set` | ✘ (read only) |
| more events: buf_new, win_*, yank_post, lsp_attach/detach | ✔ | ✔ | ＋ (no `buf_read`/`buf_unload` split; no `focus_*`) |

What is still ✘ is small: `expr`/`nowait`/`silent` keymap options, a `:normal!`
that ignores mappings, per-window options and exact window sizes, injected
languages in `winter.ts`, and moving-mark (extmark) queries.
