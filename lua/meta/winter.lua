---@meta winter
-- LuaLS definitions for Winter's Lua API.
--
-- Not loaded by Winter: this file only feeds lua_ls (completion, hover docs,
-- diagnostics) while you edit your config. Point lua_ls at the directory
-- holding it (see .luarc.json in the repo) and `winter` is known everywhere in
-- init.lua.
--
-- HOW A CONFIG LOADS
--   * `$XDG_CONFIG_HOME/winter/init.lua`, else `~/.config/winter/init.lua`.
--   * `require` looks in that directory (`?.lua`, `?/init.lua`) and then in
--     `/etc/xdg/winter/themes`.
--   * `:reload` (or a save of any `.lua` file under the config directory while
--     the `hotreload` option is on) re-runs it. Every `.lua` file there must at
--     least compile or the reload is refused and the running config stays.
--   * A reload first drops everything the config set up: keymaps, actions,
--     operators/motions/objects, commands, event handlers, timers, the
--     statusline layout, theme overrides, snippets, options (global and
--     buffer-local), new globals and `require`d modules. So register things
--     unconditionally in init.lua and never rely on state surviving it.
--   * `:lua <code>` evaluates a snippet; `:source <file>` runs one file.
--
-- CONVENTIONS
--   * Lines and columns are 1-based; columns are BYTE columns. Ranges are
--     half-open `[start, end)` unless stated otherwise.
--   * Float / overlay screen cells (`winter.float`) are 0-based.
--   * Callbacks run protected: an error shows its first line in the message bar
--     (the full traceback is kept as the last error) and never crashes the
--     editor. A statusline component that throws is hidden until redefined.
--   * Functions taking an options table ignore unknown keys.

--------------------------------------------------------------------------
-- Shared types
--------------------------------------------------------------------------

---A colour: `0xRRGGBB` or `"#rrggbb"` (exactly six hex digits).
---@alias winter.Color integer|string

---Text style. Unset fields leave whatever is underneath alone.
---@class winter.Style
---@field fg? winter.Color
---@field bg? winter.Color
---@field ul? winter.Color Underline / undercurl colour.
---@field bold? boolean
---@field dim? boolean
---@field italic? boolean
---@field underline? boolean
---@field undercurl? boolean
---@field reverse? boolean
---@field strikethrough? boolean

---A position range. `line1..line2` are inclusive 1-based lines; `col1` is the
---first byte column and `col2` the exclusive end column (byte, 1-based).
---@class winter.Range
---@field line1 integer
---@field col1 integer
---@field line2 integer
---@field col2 integer
---@field linewise boolean

---Mode words accepted by keymaps. `"visual"` covers `v` and `x`.
---@alias winter.ModeName "normal"|"insert"|"replace"|"visual"|"command"|"search"|"files"

---Where a keymap applies. Any of:
---  letters: `n` normal, `i` insert, `r` replace, `v`/`x` visual, `c` command,
---           `s` search, `f` file tree; several at once: `"nv"`
---  a word:  `"normal"`
---  a table of either: `{ "n", "x" }`, `{ "normal", "visual" }`, `{ "nx", "i" }`
---@alias winter.Modes string|(string|winter.ModeName)[]

---Border style of floats, inputs, pickers and menus.
---@alias winter.Border "rounded"|"straight"|"double"|"block"

---Key notation. Special keys are written in angle brackets, case-insensitive:
---`<Esc>` `<CR>` (`<Enter>`, `<Return>`) `<Tab>` `<BS>` `<Space>` `<lt>` (a
---literal `<`) `<Up>` `<Down>` `<Left>` `<Right>` `<Home>` `<End>` `<PageUp>`
---`<PageDown>` `<Del>` (`<Delete>`) `<C-BS>`.
---
---Modifiers `C-` `A-` (or `M-`) `S-` combine with a key: `<C-x>`, `<A-Space>`,
---`<C-S-Left>`. A combination a terminal cannot send raises an error. Anything
---else, including a bare `<x>`, is typed literally (`gd`, `<space>f`, `]d`).
---There is no `<leader>`.
---@alias winter.Keys string

--------------------------------------------------------------------------
-- The global
--------------------------------------------------------------------------

---Everything Winter exposes to Lua lives in this one global.
---@class winter
---Winter's version string.
---@field version string
---Global editor options, same names as `:set` (aliases work). Reading an
---unknown name or assigning a bad value raises; `winter.options.set` reports
---`false, reason` instead.
---@field opt winter.OptionTable
---Buffer-local view of the options that have a local form (`tabstop`,
---`shiftwidth`, `expandtab` and their aliases). Writing any other option
---raises. Reading falls back to the global value. Inside an event handler it
---targets the buffer the event is about (`file_type` fires before that buffer
---is current), otherwise the current buffer.
---@field opt_local winter.LocalOptionTable
winter = {}

---Show the arguments (tab separated, like `tostring`) in the message bar.
---@param ... any
function print(...) end

---Current mode name.
---`"operator"` is operator-pending (after `d`, `y`, ... before the motion).
---@return "normal"|"insert"|"visual"|"replace"|"command"|"search"|"operator"|"files"
function winter.mode() end

---The visual selection, or `nil` outside visual mode.
---@return winter.Range|nil
function winter.selection() end

--------------------------------------------------------------------------
-- Options
--------------------------------------------------------------------------

---Every global option. Names match `:set`; aliases are listed too.
---@class winter.OptionTable
---@field tabstop integer Display width of a `\t` (1..16, default 4)
---@field ts integer Alias of `tabstop`.
---@field shiftwidth integer One indent level (1..16, default 4)
---@field sw integer Alias of `shiftwidth`.
---@field expandtab boolean The Tab key and indents insert spaces (default true)
---@field et boolean Alias of `expandtab`.
---@field paste_indent boolean Re-indent pasted text (default true)
---@field pi boolean Alias of `paste_indent`.
---@field indent_engine integer 0 structural with tree-sitter fallback, 1 tree-sitter only, 2 structural only (0..2, default 0)
---@field ie integer Alias of `indent_engine`.
---@field indentnamespace integer Indent inside `namespace` blocks: 0 flat, 1 indent, 2 follow the block's first line (0..2, default 2)
---@field inam integer Alias of `indentnamespace`.
---@field indentcase integer Indent `case` labels in a `switch`: 0 flat, 1 indent, 2 follow the block's first line (0..2, default 2)
---@field icase integer Alias of `indentcase`.
---@field quit_times integer Extra Ctrl-Q presses needed while there are unsaved changes (0..99, default 3)
---@field history integer History entries kept per file (0..10000, default 500)
---@field hi integer Alias of `history`.
---@field remember_cursor boolean Remember the cursor position per file (default true)
---@field undofile boolean Persist undo history per file (default true)
---@field udf boolean Alias of `undofile`.
---@field undolevels integer Edits kept in the undo history per file (0..100000, default 1000)
---@field ul integer Alias of `undolevels`.
---@field timeoutlen integer Key-chord timeout in ms (0..60000, default 1000)
---@field tm integer Alias of `timeoutlen`.
---@field hotreload boolean Reload the config when a `.lua` file under the config directory changes (default true)
---@field hr boolean Alias of `hotreload`.
---@field formatonsave boolean Run the formatter before every write (default false). `winter.format.setup{ on_save = ... }` and `:format on|off|toggle` set it too.
---@field fos boolean Alias of `formatonsave`.
---@field formattimeout integer Ms a formatter may block a save (50..60000, default 1000)
---@field fto integer Alias of `formattimeout`.
---@field cursor string Cursor shape per mode, e.g. `"n:block; i:bar; v:underline; r:underline-blink"`. Shapes: `default`, `block`, `underline`, `bar` (or `beam`); append `-blink` to blink (not for `default`). Modes: `n` `v` `i` `r`. Only the modes you list change.
---@field cursor_inverse boolean Semantic cursor colouring (default false)
---@field number boolean Show line numbers (default false)
---@field nu boolean Alias of `number`.
---@field relativenumber boolean Show relative line numbers (default false)
---@field rnu boolean Alias of `relativenumber`.
---@field numberwidth integer Gutter width (1..10, default 4)
---@field nuw integer Alias of `numberwidth`.
---@field wrap boolean Wrap long lines at the screen edge (default false)
---@field wrap_indicator boolean Prefix wrapped continuation rows with a marker (default false)
---@field wrapind boolean Alias of `wrap_indicator`.
---@field indent_leads boolean Show a chevron at the start of each indent level (default false)
---@field ileads boolean Alias of `indent_leads`.
---@field scrolloff integer Lines kept visible above/below the cursor (0..999, default 5)
---@field so integer Alias of `scrolloff`.
---@field scrolleof boolean Allow scrolling past the end of the file (default false)
---@field cursorline boolean Tint the cursor row (default false)
---@field cul boolean Alias of `cursorline`.
---@field colorcolumn integer 1-based display column to tint, 0 = off (0..500, default 0)
---@field cc integer Alias of `colorcolumn`.
---@field hlsearch boolean Highlight search matches (default true)
---@field hls boolean Alias of `hlsearch`.
---@field matchparen boolean Highlight the matching bracket (default false)
---@field mp boolean Alias of `matchparen`.
---@field hltrailing boolean Highlight trailing whitespace (default false)
---@field hlt boolean Alias of `hltrailing`.
---@field rainbow boolean Rainbow delimiters (default true)
---@field rb boolean Alias of `rainbow`.
---@field border winter.Border Border style for floats, pickers and menus (default `"rounded"`).
---@field bs winter.Border Alias of `border`.
---@field nerdicons boolean Use Nerd Font icons in the UI (default false)
---@field ni boolean Alias of `nerdicons`.
---@field diagnostics boolean Show LSP diagnostics (default true)
---@field diag boolean Alias of `diagnostics`.
---@field diag_virtual_text boolean End-of-line diagnostic message (default true)
---@field diag_undercurl boolean Curly underline for diagnostics (default true)
---@field diag_severity integer Show this severity and worse: 1 error .. 4 hint (1..4, default 4)
---@field semantic_tokens boolean LSP semantic-token highlighting (default true)
---@field sem boolean Alias of `semantic_tokens`.
---@field hlreference boolean Highlight other uses of the symbol under the cursor (LSP) (default false)
---@field hlr boolean Alias of `hlreference`.
---@field treewidth integer Tree columns, before the separator (10..80, default 30)
---@field treeicons boolean Tree icons only, no other UI icons (default false)
---@field ti boolean Alias of `treeicons`.
---@field treehidden boolean Show dotfiles when the tree is first opened (default false)
---@field treefollow boolean Keep the cursor on the active buffer's file (default true)
---@field treegit boolean Git status marks (default true)
---@field treegitnames boolean Recolour file names by git status (default true)

---The options that have a buffer-local form. Reading any other option falls
---back to its global value; writing one raises.
---@class winter.LocalOptionTable
---@field tabstop integer
---@field ts integer
---@field shiftwidth integer
---@field sw integer
---@field expandtab boolean
---@field et boolean

---@class winter.OptionInfo
---@field name string
---@field alias? string
---@field type "boolean"|"integer"|"cursor"|"border"
---@field min? integer Integers only.
---@field max? integer Integers only.
---@field value boolean|integer|string

---Function-style option access.
---@class winter.options
winter.options = {}

---Every option with its type and current (global) value.
---@return winter.OptionInfo[]
function winter.options.list() end

---@param name string
---@return winter.OptionInfo|nil # nil for an unknown option
function winter.options.info(name) end

---@param name string
---@return boolean|integer|string|nil # nil for an unknown option
function winter.options.get(name) end

---Set an option without raising.
---@param name string
---@param value boolean|integer|string
---@return boolean ok
---@return "unknown option"|"invalid value"|nil reason
function winter.options.set(name, value) end

---Flip a boolean option and return the new value. Raises for other types.
---@param name string
---@return boolean
function winter.options.toggle(name) end

--------------------------------------------------------------------------
-- Buffers
--------------------------------------------------------------------------

---@class winter.buf
winter.buf = {}

---@class winter.BufInfo
---@field index integer 1-based position in the buffer list.
---@field name string File path, `""` for an unnamed buffer.
---@field lang string Tree-sitter language, `""` if none.
---@field modified boolean
---@field lines integer
---@field current boolean
---@field nofile boolean Scratch / output buffers that never save.

---Line `n` of the current buffer (default: the cursor line), or `nil` when out
---of range.
---@param n? integer
---@return string|nil
function winter.buf.line(n) end

---Number of lines in the current buffer.
---@return integer
function winter.buf.line_count() end

---Lines `first..last` (inclusive, clamped to the buffer; default: all).
---@param first? integer
---@param last? integer
---@return string[]
function winter.buf.lines(first, last) end

---Replace lines `first..last` (inclusive) with `list`. `last == first - 1`
---inserts before `first` (`first == line_count() + 1` appends); an empty list
---deletes. One undo step outside insert mode. Raises for out-of-range lines or
---a non-string item.
---@param first integer
---@param last integer
---@param list string[]
function winter.buf.set_lines(first, last, list) end

---Text in `[start, end)`. Columns are clamped to their line; raises if the
---lines are out of range or the end is before the start.
---@param line1 integer
---@param col1 integer
---@param line2 integer
---@param col2 integer
---@return string
function winter.buf.text(line1, col1, line2, col2) end

---Insert text at the cursor; the cursor ends up after it. One undo step outside
---insert mode.
---@param text string
function winter.buf.insert(text) end

---Swap `[start, end)` for `text` and put the cursor after it. One undo step
---outside insert mode. Raises like `winter.buf.text`.
---@param line1 integer
---@param col1 integer
---@param line2 integer
---@param col2 integer
---@param text string
function winter.buf.replace(line1, col1, line2, col2, text) end

---Path of the current buffer (`""` if unnamed).
---@return string
function winter.buf.name() end

---Tree-sitter language of the current buffer (`""` if none).
---@return string
function winter.buf.lang() end

---All buffers, in buffer-list order.
---@return winter.BufInfo[]
function winter.buf.list() end

---One buffer (default: current); `nil` when the index is out of range.
---@param index? integer
---@return winter.BufInfo|nil
function winter.buf.info(index) end

---Index of the current buffer.
---@return integer
function winter.buf.current() end

---Switch by 1-based index, or by name (matched like `:buffer`).
---@param target integer|string
---@return boolean found
function winter.buf.switch(target) end

---Open a file (or switch to it if already open). Returns its buffer index.
---@param path string
---@return integer index
function winter.buf.open(path) end

---Close a buffer (default: current). Refuses a modified one unless `force`.
---@param index? integer
---@param force? boolean
---@return boolean closed
function winter.buf.close(index, force) end

---Move through the buffer list, wrapping. Default step is 1.
---@param step? integer
function winter.buf.cycle(step) end

---Go to the alternate buffer.
function winter.buf.alt() end

---Write the current buffer.
function winter.buf.save() end

---Write every modified, named buffer.
function winter.buf.save_all() end

---@param index? integer Default: current.
---@return boolean # `false` for an out-of-range index
function winter.buf.modified(index) end

---@class winter.cursor
winter.cursor = {}

---Cursor position: 1-based line and byte column.
---@return integer line
---@return integer col
function winter.cursor.get() end

---Move the cursor (clamped to the buffer, snapped to a character boundary).
---@param line integer
---@param col? integer Default 1.
function winter.cursor.set(line, col) end

---@class winter.win
winter.win = {}

---Text area size.
---@return integer rows
---@return integer cols
function winter.win.size() end

---First visible line.
---@return integer
function winter.win.top() end

---@class winter.jump
winter.jump = {}

---Jumplist back (default 1 step).
---@param count? integer
function winter.jump.older(count) end

---Jumplist forward (default 1 step).
---@param count? integer
function winter.jump.newer(count) end

--------------------------------------------------------------------------
-- Keymaps and actions
--------------------------------------------------------------------------

---Built-in action names usable in `winter.keymap.set` and `winter.action.run`.
---Not exhaustive (motions and operators register more): `winter.action.list()`
---has the full set. Any string is accepted, so your own `winter.action.add`
---names work too.
---@alias winter.BuiltinAction
---| "mode.insert" | "mode.append" | "mode.append_eol" | "mode.insert_bol"
---| "mode.replace" | "mode.visual" | "mode.visual_line" | "mode.command"
---| "mode.search"
---| "edit.open_below" | "edit.open_above" | "edit.undo" | "edit.redo"
---| "edit.dot" | "edit.comment" | "edit.replace_char" | "edit.paste_after"
---| "edit.paste_before" | "edit.x" | "edit.X" | "edit.D"
---| "edit.C" | "edit.s" | "edit.S" | "edit.Y"
---| "motion.left" | "motion.right" | "motion.down" | "motion.up"
---| "motion.line" | "motion.line_start" | "motion.line_end" | "motion.first_nonblank"
---| "motion.word_next" | "motion.word_prev" | "motion.word_end" | "motion.WORD_next"
---| "motion.WORD_prev" | "motion.WORD_end" | "motion.para_fwd" | "motion.para_back"
---| "motion.match_pair" | "motion.file_start" | "motion.file_end" | "find.f"
---| "find.F" | "find.t" | "find.T" | "find.repeat"
---| "find.repeat_reverse"
---| "op.delete" | "op.yank" | "op.change" | "op.indent"
---| "op.dedent" | "op.reindent"
---| "visual.char" | "visual.line" | "visual.swap" | "visual.delete"
---| "visual.delete_x" | "visual.change" | "visual.change_s" | "visual.yank"
---| "visual.indent" | "visual.dedent" | "visual.comment" | "visual.command"
---| "visual.dup" | "visual.surround" | "visual.paste" | "visual.paste_keep"
---| "visual.line_end" | "visual.right"
---| "lsp.definition" | "lsp.type_definition" | "lsp.implementation" | "lsp.references"
---| "lsp.hover" | "lsp.rename" | "lsp.code_action" | "lsp.code_action_visual"
---| "lsp.switch_source_header"
---| "tree.toggle" | "buffer.next" | "buffer.prev" | "buffer.alt"
---| "diag.next" | "diag.prev" | "jump.older" | "jump.newer"
---| "search.next" | "search.prev"

---Passed to every action function.
---@class winter.ActionCtx
---@field count integer Typed count, 0 if none.
---@field arg string Extra character for actions registered with `wants_char`, else `""`.

---@alias winter.ActionFn fun(ctx: winter.ActionCtx)

---@class winter.ActionOpts
---`wants_char`: read one more key after the mapping and pass it as `ctx.arg`.
---@field wants_char? boolean

---A step of an action chain written as a table: `{ "name", count = 3, arg = "x" }`.
---@class winter.ActionStepTable
---@field [1] string Action name.
---@field count? integer
---@field arg? string

---One step of an action chain: an action name, or a table with its own
---`count` / `arg`.
---@alias winter.ActionStep string|winter.ActionStepTable

---@class winter.keymap
winter.keymap = {}

---Map keys to a built-in action name (`"lsp.definition"`, `"tree.toggle"`, ...)
---or a Lua function. Mapping over an existing key replaces it. A function is
---registered as an anonymous action and freed when its last key is unmapped.
---Raises on bad modes or key notation.
---@param modes winter.Modes
---@param keys winter.Keys
---@param action winter.BuiltinAction|string|winter.ActionFn
---@param opts? winter.ActionOpts
function winter.keymap.set(modes, keys, action, opts) end

---Remove a mapping. Returns whether anything was mapped (in any of `modes`).
---@param modes winter.Modes
---@param keys winter.Keys
---@return boolean
function winter.keymap.del(modes, keys) end

---@class winter.action
winter.action = {}

---Register a named action (usable from `keymap.set` and `action.run`).
---@param name string
---@param fn winter.ActionFn
---@param opts? winter.ActionOpts
function winter.action.add(name, fn, opts) end

---Run an action by name. `false` if there is none (or it declined to run).
---@overload fun(steps: winter.ActionStep[], defaults?: { count?: integer, arg?: string }): boolean, string?, integer?
---@param name winter.BuiltinAction|string
---@param ctx? { count?: integer, arg?: string }
---@return boolean found
function winter.action.run(name, ctx) end

---Names of all actions, built in and yours.
---@return string[]
function winter.action.list() end

--------------------------------------------------------------------------
-- Commands
--------------------------------------------------------------------------

---@class winter.CmdArgs
---@field args string Everything after the command name.
---@field bang boolean Called with `!`.
---@field range boolean A range was given.
---@field line1 integer First line of the range (1-based); `0` when `range` is false.
---@field line2 integer Last line of the range; `0` when `range` is false.

---@class winter.CmdCandidate
---@field text string Replaces the word being completed.
---@field detail? string Shown dim next to it.

---@alias winter.CmdCompletion string|winter.CmdCandidate
---@alias winter.CmdCompleteFn fun(lead: string): winter.CmdCompletion[]

---@class winter.CmdOpts
---@field desc? string
---@field nargs? "0"|"1"|"?"|"*" Default `"*"`.
---@field bang? boolean Default true.
---@field range? boolean Default true.
---@field aliases? string[]
---@field complete? winter.CmdCompleteFn `lead` is the part of the word before the cursor.

---@class winter.cmd
winter.cmd = {}

---Define `:Name` (letters only). Redefining replaces.
---@param name string
---@param fn fun(args: winter.CmdArgs)
---@param opts? winter.CmdOpts
function winter.cmd.add(name, fn, opts) end

---Run a command line, e.g. `winter.cmd.run("s/a/b/")`.
---@param line string
function winter.cmd.run(line) end

--------------------------------------------------------------------------
-- Extending the editing model
--------------------------------------------------------------------------

---@class winter.ext
winter.ext = {}

---New operator: works as `<keys><motion>`, `<keys><keys>` (whole lines) and on a
---visual selection. Edit through `winter.buf`; one undo step.
---@param name string
---@param fn fun(range: winter.Range)
---@param keys winter.Keys
function winter.ext.operator(name, fn, keys) end

---New motion. Must not edit. Return the target; return nothing to fail. The
---optional third value picks how an operator treats the end (default
---`"exclusive"`).
---@param name string
---@param fn fun(line: integer, col: integer, count: integer, arg: string): integer?, integer?, ("exclusive"|"inclusive"|"linewise")?
---@param keys winter.Keys
---@param opts? winter.ActionOpts `wants_char` passes the extra key as `arg`.
function winter.ext.motion(name, fn, keys, opts) end

---New text object. Must not edit. Return `line1, col1, line2, col2` with an
---exclusive end, or nothing.
---@param name string
---@param fn fun(line: integer, col: integer, count: integer): integer?, integer?, integer?, integer?
---@param keys winter.Keys
function winter.ext.object(name, fn, keys) end

--------------------------------------------------------------------------
-- Events
--------------------------------------------------------------------------

---Payload of every event: `event` is its name; positional arguments are also
---available by name for the built-in ones.
---@class winter.Event
---@field event string
---@field [integer] string

---@class winter.BufEvent: winter.Event
---@field filename string

---@class winter.FileTypeEvent: winter.Event
---@field filename string
---@field lang string

---`buf_reload` has no named fields: the filename is `ev[1]`.
---@class winter.BufReloadEvent: winter.Event

---@class winter.ModeEvent: winter.Event
---@field old string
---@field new string

---@class winter.EventOpts
---Run at most once, then unsubscribe.
---@field once? boolean
---Filter. For `file_type`: language name(s), e.g. `"lua"` or `{ "c", "cpp" }`.
---For `buf_enter`, `buf_write_pre`, `buf_write_post`, `buf_reload` (and custom
---events with arguments): glob(s) matched against the full path and the
---basename, e.g. `"*.lua"`. Events without arguments (`ready`, `quit`,
---`text_changed`, `cursor_moved`, `reload`) take no pattern and raise.
---@field pattern? string|string[]

---@class winter.ev
winter.ev = {}

---Subscribe. Built-in events:
---  `ready`          first config load only (not on reload)
---  `reload`         after a config reload (`:reload` or hot reload); handlers
---                   from the new config see it
---  `quit`
---  `text_changed`, `cursor_moved`   fire very often: keep handlers cheap
---  `buf_enter`, `buf_write_pre`, `buf_write_post`   `{ filename }`
---  `buf_reload`     buffer re-read after the file changed on disk; the
---                   filename is `ev[1]`
---  `file_type`      `{ filename, lang }`; fires again for every open buffer
---                   after a reload, so per-filetype setup is re-applied
---  `mode_changed`   `{ old, new }`
---Any other name is a custom event for `winter.ev.emit`.
---
---Inside a handler `winter.opt_local` targets the buffer the event is about.
---Returns an id for `winter.ev.off`.
---@overload fun(event: "buf_enter"|"buf_write_pre"|"buf_write_post", fn: fun(ev: winter.BufEvent), opts?: winter.EventOpts): integer
---@overload fun(event: "file_type", fn: fun(ev: winter.FileTypeEvent), opts?: winter.EventOpts): integer
---@overload fun(event: "buf_reload", fn: fun(ev: winter.BufReloadEvent), opts?: winter.EventOpts): integer
---@overload fun(event: "mode_changed", fn: fun(ev: winter.ModeEvent), opts?: winter.EventOpts): integer
---@param event string
---@param fn fun(ev: winter.Event)
---@param opts? winter.EventOpts
---@return integer id
function winter.ev.on(event, fn, opts) end

---@param id integer
---@return boolean removed
function winter.ev.off(id) end

---Fire a custom event. Arguments must be strings or numbers (delivered as
---strings).
---@param event string
---@param ... string|number
function winter.ev.emit(event, ...) end

--------------------------------------------------------------------------
-- Statusline
--------------------------------------------------------------------------

---What a statusline component function receives.
---@class winter.StatusCtx
---@field mode string Lower-case mode name, as `winter.mode()`.
---@field width integer Terminal columns.
---@field line integer
---@field col integer Byte column (like `winter.cursor`).
---@field vcol integer Display column.
---@field lines integer Lines in the buffer.
---@field name string Buffer path, `""` if unnamed.
---@field lang string
---@field modified boolean
---@field bufnr integer
---@field bufs integer Number of buffers.
---@field icons boolean `winter.opt.nerdicons`.
---@field diag { error: integer, warn: integer, info: integer, hint: integer }

---A piece with its own style: `{ "text", fg = "#f38ba8" }` or
---`{ text = "text", bold = true }`.
---@class winter.StatusPieceTable: winter.Style
---@field text? string
---@field [1]? string

---@alias winter.StatusPiece string|winter.StatusPieceTable

---A whole component result as a table: pieces in the array part (or `text`),
---block-level style fields, `pad = false` to drop the one-space padding, and
---`priority` (lowest is dropped first on a narrow terminal; the built-in
---`mode` is 100, `file` 90, yours default to 50).
---@class winter.StatusBlock: winter.Style
---@field text? string
---@field pad? boolean
---@field priority? integer
---@field [integer] winter.StatusPiece

---What a component returns: `nil`/`false`/`""` hides it, a string is one
---piece, a table is a block.
---@alias winter.StatusResult string|false|winter.StatusBlock

---Built-in component names.
---@alias winter.StatusName
---| "mode" | "file" | "lines" | "buffers" | "diagnostics"
---| "filetype" | "indent" | "position" | "percent" | "gap"
---| string

---@alias winter.StatusFn fun(ctx: winter.StatusCtx): winter.StatusResult?

---Separator glyph selection for one side: a string, `true` for the automatic
---glyph, `false` for none.
---@alias winter.StatusSeparator string|boolean

---@class winter.StatusSetup
---Component names or functions (a function becomes an anonymous component).
---Default: `mode file lines buffers`.
---@field left? (winter.StatusName|winter.StatusFn)[]
---Default: `diagnostics filetype position percent`.
---@field right? (winter.StatusName|winter.StatusFn)[]
---Separators between blocks of different background: a preset, a boolean
---(`false` = `"none"`, `true` = `"auto"`), or custom glyphs per side. `"auto"`
---= powerline arrows when `nerdicons` is on.
---@field separators? "powerline"|"round"|"slant"|"none"|"auto"|boolean|{ left?: winter.StatusSeparator, right?: winter.StatusSeparator }
---@field enabled? boolean Draw the bar's components at all.

---@class winter.statusline
winter.statusline = {}

---Set the layout. Only the keys you pass change. Raises on a bad list or
---separator preset.
---
---Colours come from theme groups: `ui.statusline` (the bar),
---`ui.statusline.<component>`, and `ui.statusline.<mode>` for the mode pill
---(`normal insert visual vline replace command search files vfiles`).
---@param cfg winter.StatusSetup
function winter.statusline.setup(cfg) end

---Define (or replace) a component. `style` is its default colours; a
---`ui.statusline.<name>` theme group overrides it.
---@param name string
---@param fn winter.StatusFn
---@param opts? { priority?: integer, style?: winter.Style }
function winter.statusline.component(name, fn, opts) end

---@param name string
---@return boolean removed
function winter.statusline.remove(name) end

---All component names, built-in and yours.
---@return string[]
function winter.statusline.list() end

---@return { left: string[], right: string[], enabled: boolean }
function winter.statusline.layout() end

---Back to the default layout (also drops anonymous function components).
function winter.statusline.reset() end

--------------------------------------------------------------------------
-- Theme
--------------------------------------------------------------------------

---Built-in `ui.*` / `diag.*` group names (what `winter.theme.groups()`
---returns). Any other string is taken as a tree-sitter capture name
---(`"comment"`, `"function"`, `"type.builtin"`, ...).
---
---Also recognised, but not listed by `groups()`: `md.*` (markdown rendering:
---`md.heading.bg` `md.bullet` `md.code` `md.code.block` `md.code.fence`
---`md.code.lang` `md.emph` `md.strong` `md.strike` `md.link` `md.link.url`
---`md.marker` `md.quote` `md.rule` `md.table.head` `md.table.border`
---`md.checkbox.checked` `md.checkbox.unchecked`), `ui.tree.git.staged`,
---`ui.tree.name.<file name>`, `ui.tree.ext.<extension>`,
---`ui.tree.icon.name.<file name>`, `ui.tree.icon.ext.<extension>`, and
---`lsp.<semantic token type>` (`lsp.function`, `lsp.parameter`, ...) plus
---`lsp.variable`.
---@alias winter.ThemeGroup
---| "ui.normal" | "ui.statusline" | "ui.message"
---| "ui.linenr" | "ui.linenr.cursor" | "ui.tilde"
---| "ui.visual" | "ui.search" | "ui.search.current"
---| "ui.search.cursor" | "ui.cursor" | "ui.tree.file"
---| "ui.tree.dir" | "ui.tree.hidden" | "ui.tree.guide"
---| "ui.tree.sep" | "ui.tree.select" | "ui.tree.visual"
---| "ui.menu.select" | "ui.float" | "ui.float.border"
---| "diag.error" | "ui.tree.root" | "ui.tree.open"
---| "ui.tree.active" | "ui.tree.match" | "ui.tree.symlink"
---| "ui.tree.icon.dir" | "ui.tree.icon.file" | "ui.tree.git.modified"
---| "ui.tree.git.added" | "ui.tree.git.deleted" | "ui.tree.git.untracked"
---| "ui.tree.git.conflict" | "diag.warn" | "diag.info"
---| "diag.hint" | "ui.welcome" | "ui.ctrl"
---| "ui.statusline.file" | "ui.statusline.modified" | "ui.statusline.lines"
---| "ui.statusline.buffers" | "ui.statusline.diagnostics" | "ui.statusline.filetype"
---| "ui.statusline.indent" | "ui.statusline.position" | "ui.statusline.percent"
---| "ui.statusline.command" | "ui.statusline.search" | "ui.statusline.files"
---| "ui.picker.border" | "ui.picker.prompt" | "ui.menu"
---| "ui.menu.marker" | "ui.menu.dir" | "ui.menu.match"
---| "ui.menu.detail" | "ui.menu.kind" | "ui.menu.disabled"
---| "ui.float.title" | "ui.input" | "ui.input.border"
---| "ui.input.prompt" | "ui.statusline.normal" | "ui.statusline.insert"
---| "ui.statusline.visual" | "ui.statusline.vline" | "ui.statusline.vfiles"
---| "ui.statusline.replace" | "diag.virtual" | "diag.underline"
---| "diag.message" | "ui.menu.doc" | "ui.menu.key"
---| "ui.hover" | "ui.hover.code" | "ui.hover.heading"
---| "ui.cursorline" | "ui.colorcolumn" | "ui.matchparen"
---| "ui.trailing" | "ui.indent_lead" | "ui.wrap"
---| "ui.snippet" | "ui.snippet.active"
---| "ui.reference" | "ui.reference.read" | "ui.reference.write"
---| "ui.subst.match" | "ui.subst.replace" | "ui.tree.git.staged"
---| string

---@class winter.theme
winter.theme = {}

---Style a group. `false`/`nil` removes the override. Raises on a bad colour.
---@param group winter.ThemeGroup
---@param style winter.Style|false|nil
function winter.theme.set(group, style) end

---Set many groups at once: `{ [group] = style | false }`.
---@param groups table<winter.ThemeGroup, winter.Style|false>
function winter.theme.apply(groups) end

---The override of a group (colours as `"#rrggbb"`, only the flags that are on),
---or `nil` if none is set.
---@param group winter.ThemeGroup
---@return winter.Style|nil
function winter.theme.get(group) end

---Drop every override and the rainbow palette.
function winter.theme.reset() end

---Colours for rainbow delimiters; no argument = built-in palette.
---@param colors? winter.Color[]
function winter.theme.rainbow(colors) end

---Built-in `ui.*` / `diag.*` group names.
---@return string[]
function winter.theme.groups() end

---Make group `from` look like `to`. `false`/`nil` unlinks.
---@param from winter.ThemeGroup
---@param to winter.ThemeGroup|false|nil
function winter.theme.link(from, to) end

--------------------------------------------------------------------------
-- UI: messages, input, choosers, pickers
--------------------------------------------------------------------------

---@class winter.ui
winter.ui = {}

---Show a message in the message bar.
---@param msg string
function winter.ui.status(msg) end

---@class winter.InputOpts
---@field prompt? string
---@field default? string
---@field w? integer Default 40.
---@field at? "center"|"cursor"|"screen" Default `"center"`.
---@field x? integer
---@field y? integer
---@field border? winter.Border
---@field style? winter.Style
---@field on_change? fun(text: string)
---@field on_submit? fun(text: string)
---@field on_cancel? fun()

---Single-line input box. Returns its overlay id.
---@param opts winter.InputOpts
---@return integer id
function winter.ui.input(opts) end

---@class winter.SelectOpts
---@field prompt? string Default `"Select"`.
---Label of an item. Without it: the item itself if a string or number, else
---its `text`, `name` or `label` field.
---@field format? fun(item: any): string

---nvim-style chooser. `on_choice` gets the chosen item (the value you passed,
---not a copy) and its index, or `nil` when cancelled.
---@generic T
---@param items T[]
---@param opts? winter.SelectOpts
---@param on_choice fun(item: T|nil, index?: integer)
function winter.ui.select(items, opts, on_choice) end

---@class winter.PickerItem
---@field text string Shown and fuzzy matched.
---@field detail? string Shown dim to the right.
---@field path? string File for the preview pane.
---@field line? integer
---@field col? integer

---@class winter.PickerOpts
---@field prompt? string Default `"Pick"`.
---@field items? (string|winter.PickerItem)[]
---@field fuzzy? boolean Default true. `false`: you filter via `on_query`.
---@field preview? boolean Preview pane (items need `path`/`line`).
---@field paths? boolean Items are file paths: icon + dimmed directory.
---@field max_results? integer
---`item` is the entry of the list you passed (the same value, not a copy).
---@field on_accept? fun(item: string|winter.PickerItem, index: integer)
---@field on_close? fun()
---Called as the query changes; answer with `winter.picker.set_items`.
---@field on_query? fun(text: string)

---@class winter.picker
winter.picker = {}

---Open a picker. Raises if an item is not a string or a table with `text`.
---@param opts winter.PickerOpts
function winter.picker.open(opts) end

---Replace what the open picker shows. Returns whether one is open.
---@param items (string|winter.PickerItem)[]
---@return boolean
function winter.picker.set_items(items) end

function winter.picker.close() end

---@return boolean
function winter.picker.active() end

---The built-in file picker.
function winter.picker.files() end

---The built-in recent-files picker (skips the file you are in).
function winter.picker.recent() end

---The built-in live grep picker.
function winter.picker.grep() end

--------------------------------------------------------------------------
-- Floating windows
--------------------------------------------------------------------------

---@class winter.FloatOpts
---@field at? "cursor"|"screen" Default `"cursor"`. `x`/`y` are 0-based cells from the cursor or the screen origin.
---@field x? integer Default 0.
---@field y? integer Default 1 for `at = "cursor"` (the row below), else 0.
---@field w? integer Sized from `lines` when omitted.
---@field h? integer
---@field z? integer Stacking order.
---@field lines? string[] Initial text (also sizes the float).
---@field style? winter.Style
---@field border? boolean|winter.Border
---@field border_style? winter.Style
---@field cover_bars? boolean Draw over the statusline.
---@field dismiss_on_move? boolean Close when the cursor moves. Default true.
---@field esc_closes? boolean Default true.
---@field visible? boolean Default true.
---@field modal? boolean Capture input while open (keys go to `on_key`); implies `dismiss_on_move = false`.
---@field pass? string[] Single-character keys that fall through to the editor. Default `{ ":" }`; giving a list replaces it.
---@field cursor? integer[] `{x, y}` cursor position inside the float.
---Key handler: `name` is key notation (`"<Esc>"`, `"<C-x>"`, `"j"`), `code` the
---raw key code. Return true to consume.
---@field on_key? fun(name: string, code: integer): boolean?
---@field on_close? fun()
---Redraw hook with the float's id and size.
---@field on_paint? fun(id: integer, w: integer, h: integer)

---@class winter.float
winter.float = {}

---Open a floating window. Returns its id. Raises on a bad `at`, `border` or
---colour.
---@param opts winter.FloatOpts
---@return integer id
function winter.float.open(opts) end

---Draw text at `x, y` (0-based, inside the float's border). Returns the end
---column.
---@param id integer
---@param x integer
---@param y integer
---@param text string
---@param style? winter.Style
---@return integer end_x
function winter.float.put(id, x, y, text, style) end

---Blank the float with a background.
---@param id integer
---@param style? winter.Style
function winter.float.clear(id, style) end

---@param id integer
function winter.float.close(id) end

---@param id integer
---@param x integer
---@param y integer
function winter.float.cursor(id, x, y) end

--------------------------------------------------------------------------
-- Decorations
--------------------------------------------------------------------------

---@class winter.deco
winter.deco = {}

---Create (or look up) a decoration namespace.
---@param name string
---@return integer ns
function winter.deco.ns(name) end

---@class winter.DecoStyle: winter.Style
---@field prio? integer Orders overlapping decorations (higher wins).

---Restyle `[start, end)` in the current buffer; both ends follow edits.
---Raises for out-of-range lines or an end before the start.
---@param ns integer
---@param line1 integer
---@param col1 integer
---@param line2 integer
---@param col2 integer
---@param style? winter.DecoStyle
function winter.deco.span(ns, line1, col1, line2, col2, style) end

---Virtual text after the end of a line; it follows the line as text changes.
---Only the first line of `text` is shown. `prio` orders items on one line
---(higher is leftmost; diagnostics are 99).
---@param ns integer
---@param line integer
---@param text string
---@param style? winter.DecoStyle
function winter.deco.eol(ns, line, text, style) end

---Remove everything in a namespace (current buffer).
---@param ns integer
function winter.deco.clear(ns) end

--------------------------------------------------------------------------
-- Jobs and timers
--------------------------------------------------------------------------

---@class winter.JobOpts
---@field on_stdout? fun(chunk: string)
---@field on_stderr? fun(chunk: string) When set, stderr is kept apart from stdout.
---@field on_exit? fun(code: integer, output: string)
---@field stdin? string Text to feed the process.
---@field pipe_stdin? boolean Keep stdin open for `winter.job.write`.
---@field keep_output? boolean Collect output for `on_exit`. Default: only if there is no `on_stdout`.

---@class winter.job
winter.job = {}

---Run a command through the shell, asynchronously. Returns the job id, or
---`nil` if it could not start.
---@param cmd string
---@param opts? winter.JobOpts
---@return integer|nil id
function winter.job.start(cmd, opts) end

---Shorthand: run and get the whole output once it finishes.
---@param cmd string
---@param on_done fun(output: string, code: integer)
---@param stdin? string
---@return integer|nil id
function winter.job.run(cmd, on_done, stdin) end

---@param id integer
---@param data string
---@return boolean
function winter.job.write(id, data) end

---@param id integer
function winter.job.close_stdin(id) end

---@param id integer
function winter.job.kill(id) end

---@class winter.timer
winter.timer = {}

---Call `fn` once after `ms` milliseconds (minimum 1). Returns a timer id.
---@param ms integer
---@param fn fun()
---@return integer id
function winter.timer.after(ms, fn) end

---Call `fn` every `ms` milliseconds (minimum 1) until cancelled.
---@param ms integer
---@param fn fun()
---@return integer id
function winter.timer.every(ms, fn) end

---@param id integer
---@return boolean cancelled
function winter.timer.cancel(id) end

--------------------------------------------------------------------------
-- Diagnostics and LSP
--------------------------------------------------------------------------

---@class winter.Diagnostic
---@field line integer
---@field col integer
---@field end_line integer
---@field end_col integer
---@field severity 1|2|3|4 1 error, 2 warning, 3 info, 4 hint.
---@field severity_name string
---@field message string
---@field source string

---@class winter.diag
winter.diag = {}

---Diagnostics of a buffer (default: current). Empty for a bad index.
---@param bufnr? integer
---@return winter.Diagnostic[]
function winter.diag.get(bufnr) end

---@param bufnr? integer
---@return { error: integer, warn: integer, info: integer, hint: integer }
function winter.diag.count(bufnr) end

---Jump to the next (`dir >= 0`) or previous (`dir < 0`) diagnostic.
---@param dir integer
---@param count? integer
function winter.diag.jump(dir, count) end

---A server starts when a buffer of one of its `filetypes` is opened; it needs
---`cmd`, either given here or read from `<lspconfig>/lsp/<name>.lua`. Calling
---`setup` also enables the server (`enabled = false` turns it off).
---@class winter.LspServer
---@field cmd? string[] Command line. Required unless `lspconfig` supplies it.
---@field filetypes? string[] Tree-sitter language names.
---@field settings? table Served to the server's `workspace/configuration` requests.
---@field init_options? table Sent as `initializationOptions`.
---@field enabled? boolean Default true. Not allowed for `"*"`.
---Directory of an nvim-lspconfig checkout to read server definitions from.
---@field lspconfig? string
---@field [string] any

---`winter.lsp.goto` is a reserved word in Lua, so call it as
---`winter.lsp["goto"]("definition")`.
---@class winter.lsp
---@field ["goto"] fun(kind: "definition"|"declaration"|"type_definition"|"implementation"|"references") Jump to a symbol via the language server.
winter.lsp = {}

---Configure (and enable) a server by name; tables merge deeply over earlier
---calls and over the lspconfig definition. The name `"*"` sets defaults for
---every server. Raises on a bad option.
---@param name string
---@param opts? winter.LspServer
function winter.lsp.setup(name, opts) end

---clangd: switch between source and header.
function winter.lsp.switch_source_header() end

---Open the code-action menu at the cursor.
function winter.lsp.code_action() end

---Rename the symbol under the cursor; `preset` pre-fills the prompt.
---@param preset? string
function winter.lsp.rename(preset) end

--------------------------------------------------------------------------
-- Formatting
--------------------------------------------------------------------------

---A formatter: reads the buffer on stdin, writes the result to stdout.
---`$FILENAME`, `$DIRNAME`, `$BASENAME` and `$EXT` are expanded in `cmd`/`cwd`.
---An empty output leaves the buffer alone.
---@class winter.FormatterDef
---@field cmd string[]
---@field cwd? string Default `"$DIRNAME"`.

---Formatters to run for one filetype, in order, each fed the previous output.
---`"lsp"` is a valid name (the server's formatter).
---@class winter.FormatRule
---@field [integer] string
---@field stop_after_first? boolean Run only the first formatter that is installed.

---@class winter.FormatSetup
---@field on_save? boolean Same as the `formatonsave` option.
---@field lsp_fallback? boolean Use the server's formatter when nothing is configured for the file (default true).
---@field timeout? integer Same as `formattimeout` (50..60000).
---Add or override formatters by name. Built in: `stylua` `prettier`
---`prettierd` `clang-format` `rustfmt` `gofmt` `goimports` `black` `shfmt`
---`google-java-format`.
---@field formatters? table<string, winter.FormatterDef>
---Per tree-sitter language (`"lua"`) or extension (`".h"`); the language wins.
---`false` disables formatting for that key.
---@field by_ft? table<string, winter.FormatRule|false>

---@class winter.format
winter.format = {}

---Only the keys you pass change.
---@param cfg winter.FormatSetup
function winter.format.setup(cfg) end

---Format the current buffer now (same as `:format`).
function winter.format.run() end

--------------------------------------------------------------------------
-- Snippets, languages, file tree
--------------------------------------------------------------------------

---@class winter.snippet
winter.snippet = {}

---Expand snippet text (`${1:placeholder}`, `$0`, ...) at the cursor.
---@param body string
---@return boolean expanded
function winter.snippet.expand(body) end

---Register a snippet triggered by `trigger`.
---@param trigger string
---@param body string
function winter.snippet.add(trigger, body) end

---@class winter.lang
winter.lang = {}

---Comment syntax for a language: line comments `("lua", "--")`, block
---comments `("css", "/*", "*/")`.
---@param lang string
---@param open string
---@param close? string
function winter.lang.comment(lang, open, close) end

---@class winter.tree
winter.tree = {}

---Override file-tree icons by extension: `{ lua = "\u{e620}" }`.
---@param icons table<string, string>
function winter.tree.icons(icons) end
