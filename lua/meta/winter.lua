---@meta winter
-- LuaLS definitions for Winter's Lua API. Not loaded by Winter: only for
-- completion, hover docs and diagnostics in your editor. Point lua_ls at the
-- directory holding this file (see .luarc.json in the repo) and `winter` is
-- known everywhere in init.lua.

---Everything Winter exposes to Lua lives in this one global.
---@class winter
---Winter's version string.
---@field version string
---Editor options, same names as `:set` (aliases work too). Reading an unknown
---name or assigning a bad value raises an error; `winter.options.set` returns
---`false, reason` instead.
---@field opt winter.Options
winter = {}

---Show the arguments (tab separated, like `tostring`) in the message bar.
---@param ... any
function print(...) end

---Current mode name: `"normal"`, `"insert"`, `"visual"`, `"replace"`,
---`"command"`, `"search"`, `"operator"` (operator pending) or `"files"`.
---@return string
function winter.mode() end

---The visual selection, or `nil` outside visual mode. Lines are inclusive,
---`col2` is the exclusive end column.
---@return winter.Range|nil
function winter.selection() end

---A position range. `line1..line2` are inclusive 1-based lines; `col1` is the
---first byte column, `col2` the exclusive end column (byte, 1-based).
---@class winter.Range
---@field line1 integer
---@field col1 integer
---@field line2 integer
---@field col2 integer
---@field linewise boolean

---A colour: `0xRRGGBB` or `"#rrggbb"`.
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

---Mode words accepted by keymaps. `"visual"` covers `v` and `x`.
---@alias winter.ModeName "normal"|"insert"|"replace"|"visual"|"command"|"search"|"files"

---Where a keymap applies. Any of:
---  letters: `n` normal, `i` insert, `r` replace, `v`/`x` visual, `c` command,
---           `s` search, `f` file tree; `"nv"` = several
---  a word:  `"normal"`
---  a table of either: `{ "n", "x" }`, `{ "normal", "visual" }`, `{ "nx", "i" }`
---@alias winter.Modes string|(string|winter.ModeName)[]

---@alias winter.Border "rounded"|"straight"|"double"|"block"

--------------------------------------------------------------------------
-- Options
--------------------------------------------------------------------------

---Every editor option. Names match `:set`; aliases are listed too.
---@class winter.Options
---@field tabstop integer Display width of a '\t' (range 1..16, default 4)
---@field ts integer Alias of `tabstop`.
---@field shiftwidth integer One indent level (range 1..16, default 4)
---@field sw integer Alias of `shiftwidth`.
---@field expandtab boolean Tab key / indents use spaces (default true)
---@field et boolean Alias of `expandtab`.
---@field quit_times integer Extra Ctrl-Q presses needed with unsaved changes (range 0..99, default 3)
---@field cursor string Cursor shape per mode, e.g. `"n:block; i:bar; v:underline; r:underline-blink"`. Shapes: default, block, underline, bar (beam); add `-blink` to blink. Modes: n v i r.
---@field cursor_inverse boolean Semantic cursor coloring (default false)
---@field number boolean Line num (default false)
---@field nu boolean Alias of `number`.
---@field relativenumber boolean Relative line num (default false)
---@field rnu boolean Alias of `relativenumber`.
---@field numberwidth integer Gutter width (range 1..10, default 4)
---@field nuw integer Alias of `numberwidth`.
---@field rainbow boolean Rainbow delimiters (default true)
---@field rb boolean Alias of `rainbow`.
---@field diagnostics boolean Show LSP diags (default true)
---@field diag boolean Alias of `diagnostics`.
---@field diag_virtual_text boolean End-of-line message (default true)
---@field diag_undercurl boolean Modern curly linting (default true)
---@field diag_severity integer Show this severity and worse: 1 error .. 4 hint (range 1..4, default 4)
---@field scrolloff integer Scroll off padding (range 0..999, default 5)
---@field so integer Alias of `scrolloff`.
---@field scrolleof boolean Scroll past the EOF (default false)
---@field history integer History entries per file (range 0..10000, default 500)
---@field hi integer Alias of `history`.
---@field remember_cursor boolean Cursor pos memory per file (default true)
---@field undofile boolean Undo saved state per file (default true)
---@field udf boolean Alias of `undofile`.
---@field undolevels integer # of stored edits per file (range 0..100000, default 1000)
---@field ul integer Alias of `undolevels`.
---@field timeoutlen integer Chord timeout ms (range 0..60000, default 1000)
---@field tm integer Alias of `timeoutlen`.
---@field hlsearch boolean Search match highlighting (default true)
---@field hls boolean Alias of `hlsearch`.
---@field paste_indent boolean Indent pastes (default true)
---@field pi boolean Alias of `paste_indent`.
---@field nerdicons boolean Ui nerd icon toggle (default false)
---@field ni boolean Alias of `nerdicons`.
---@field border winter.Border UI border style for floats, pickers and menus (default rounded).
---@field bs winter.Border Alias of `border`.
---@field wrap boolean Wrap long lines at screen edge (default false)
---@field indent_engine integer 0 structural + ts fallback / 1 ts only / 2 structural only (range 0..2, default 0)
---@field ie integer Alias of `indent_engine`.
---@field semantic_tokens boolean Lsp semantic token highlighting (default true)
---@field sem boolean Alias of `semantic_tokens`.
---@field treewidth integer Columns, before the separator (range 10..80, default 30)
---@field treeicons boolean No ui icons aside from the tree box glyphs (default false)
---@field ti boolean Alias of `treeicons`.
---@field treehidden boolean Show dotfiles when the tree is first opened (default false)
---@field treefollow boolean Keep the cursor on the active buffer's file (default true)
---@field treegit boolean Git status marks (default true)
---@field treegitnames boolean Recolor file names by git stats (default true)
---@field cursorline boolean Tint the cursor row (default false)
---@field cul boolean Alias of `cursorline`.
---@field matchparen boolean Highlight the matching bracket (default false)
---@field mp boolean Alias of `matchparen`.
---@field hltrailing boolean Highlight trailing whitespace (default false)
---@field hlt boolean Alias of `hltrailing`.
---@field colorcolumn integer 1-based display column to tint, 0 = off (range 0..500, default 0)
---@field cc integer Alias of `colorcolumn`.
---@field hlreference boolean Highlight other uses of the symbol under the cursor (LSP) (default false)
---@field hlr boolean Alias of `hlreference`.
---@field formatonsave boolean Run the formatter before every write (default false). `winter.format.setup{ on_save = ... }` and `:format on|off|toggle` set it too.
---@field fos boolean Alias of `formatonsave`.
---@field formattimeout integer Ms a formatter may block a save (range 50..60000, default 1000)
---@field fto integer Alias of `formattimeout`.
---@field indentnamespace integer Indent inside `namespace` blocks: 0 flat, 1 indent, 2 follow the block's first line (range 0..2, default 2)
---@field inam integer Alias of `indentnamespace`.
---@field indentcase integer Indent `case` labels in a `switch`: 0 flat, 1 indent, 2 follow the block's first line (range 0..2, default 2)
---@field icase integer Alias of `indentcase`.
---@field hotreload boolean Reload the config when a `.lua` file under the config dir changes (default true)
---@field hr boolean Alias of `hotreload`.

---Typed access to every option by name. Aliases (`ts`, `sw`, `et`, ...) work.
---@class winter.options
winter.options = {}

---@class winter.OptionInfo
---@field name string
---@field alias? string
---@field type "boolean"|"integer"|"cursor"|"border"
---@field min? integer Integers only.
---@field max? integer Integers only.
---@field value boolean|integer|string

---Every option with its type and current value.
---@return winter.OptionInfo[]
function winter.options.list() end

---@param name string
---@return winter.OptionInfo|nil # nil for an unknown option
function winter.options.info(name) end

---@param name string
---@return boolean|integer|string|nil # nil for an unknown option
function winter.options.get(name) end

---Set an option without raising: `true`, or `false` and a reason.
---@param name string
---@param value boolean|integer|string
---@return boolean ok
---@return string? reason
function winter.options.set(name, value) end

---Flip a boolean option and return the new value. Errors for other types.
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

---Lines `first..last` (inclusive, clamped; default: all).
---@param first? integer
---@param last? integer
---@return string[]
function winter.buf.lines(first, last) end

---Replace lines `first..last` (inclusive) with `list`. `last == first - 1`
---inserts before `first`; an empty list deletes. One undo step.
---@param first integer
---@param last integer
---@param list string[]
function winter.buf.set_lines(first, last, list) end

---Text in `[start, end)`.
---@param line1 integer
---@param col1 integer
---@param line2 integer
---@param col2 integer
---@return string
function winter.buf.text(line1, col1, line2, col2) end

---Insert text at the cursor; the cursor ends up after it.
---@param text string
function winter.buf.insert(text) end

---Swap `[start, end)` for `text` and put the cursor after it. One undo step
---outside insert mode.
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

---All buffers.
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
---@return boolean
function winter.buf.modified(index) end

---@class winter.cursor
winter.cursor = {}

---Cursor position: 1-based line and byte column.
---@return integer line
---@return integer col
function winter.cursor.get() end

---Move the cursor (clamped to the buffer).
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
-- Keymaps, actions, commands
--------------------------------------------------------------------------

---Key notation: `<C-x>`, `<esc>`, `<cr>`, `<tab>`, `<bs>`, `<space>`, `<lt>`;
---everything else is typed literally (`gd`, `<space>f`, `]d`). Modifiers stack
---as `<C-S-x>`, `<A-Space>`, `<S-Space>`. There is no `<leader>`.
---
---Mappings are active in normal (`n`), insert (`i`), replace (`r`), visual
---(`v`), command (`c`) and search (`s`) modes. In text-entry modes a key that
---does not start or continue a mapping falls back to its text: an unmapped
---`<S-Space>` types a space, an unmapped `<C-Space>` types nothing, and an
---unmapped `<S-CR>`, `<C-CR>` or `<A-CR>` acts like Enter.
---@alias winter.Keys string

---Built-in action names usable in `winter.keymap.set`. Not exhaustive:
---`winter.action.list()` has the full set.
---@alias winter.BuiltinAction
---| "lsp.definition" | "lsp.type_definition" | "lsp.implementation" | "lsp.references"
---| "lsp.hover" | "lsp.rename" | "lsp.code_action" | "lsp.code_action_visual"
---| "lsp.switch_source_header"
---| "tree.toggle"
---| "buffer.next" | "buffer.prev" | "buffer.alt"
---| "diag.next" | "diag.prev"
---| "jump.older" | "jump.newer"
---| "search.next" | "search.prev"
---| "mode.insert" | "mode.append" | "mode.append_eol" | "mode.insert_bol"
---| "mode.replace" | "mode.visual" | "mode.visual_line" | "mode.command" | "mode.search"

---@class winter.ActionCtx
---@field count integer Typed count, 0 if none.
---@field arg string Extra character for actions registered with `wants_char`.

---@alias winter.ActionFn fun(ctx: winter.ActionCtx)

---@class winter.keymap
winter.keymap = {}

---Map keys to a built-in action name (`"lsp.definition"`, `"tree.toggle"`, ...)
---or a Lua function. Mapping over an existing key replaces it.
---@param modes winter.Modes
---@param keys winter.Keys
---@param action winter.BuiltinAction|string|winter.ActionFn
---@param opts? { wants_char?: boolean } `wants_char`: read one more key and pass it as `ctx.arg`.
function winter.keymap.set(modes, keys, action, opts) end

---Remove a mapping. Returns whether anything was mapped (in any of `modes`).
---@param modes winter.Modes
---@param keys winter.Keys
---@return boolean
function winter.keymap.del(modes, keys) end

---@class winter.action
winter.action = {}

---Register a named action (usable from `keymap.set`).
---@param name string
---@param fn winter.ActionFn
---@param opts? { wants_char?: boolean }
function winter.action.add(name, fn, opts) end

---Run an action by name. `false` if there is none.
---@param name string
---@param ctx? { count?: integer, arg?: string }
---@return boolean found
function winter.action.run(name, ctx) end

---Names of all actions.
---@return string[]
function winter.action.list() end

---@class winter.CmdArgs
---@field args string Everything after the command name.
---@field bang boolean Called with `!`.
---@field range boolean A range was given.
---@field line1 integer
---@field line2 integer

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

---Define `:Name` (letters only).
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

---New operator: works as `<keys><motion>`, `<keys><keys>` (lines) and on a
---visual selection. Edit through `winter.buf`; one undo step.
---@param name string
---@param fn fun(range: winter.Range)
---@param keys winter.Keys
function winter.ext.operator(name, fn, keys) end

---New motion. Must not edit. Return the target; return nothing to fail.
---@param name string
---@param fn fun(line: integer, col: integer, count: integer, arg: string): integer?, integer?, ("exclusive"|"inclusive"|"linewise")?
---@param keys winter.Keys
---@param opts? { wants_char?: boolean }
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

---@class winter.ev
winter.ev = {}

---Subscribe. Built-in events:
--- `ready` (first config load; does not fire again on reload), `reload` (after
--- a config reload, `:reload` or hot reload; handlers from the new config see
--- it), `quit`, `text_changed`, `cursor_moved` (these fire often: keep
--- handlers cheap), `buf_enter`, `buf_write_pre`, `buf_write_post` (all
--- `{filename}`), `buf_reload` (buffer re-read after the file changed on disk;
--- filename is `ev[1]`), `file_type` (`{filename, lang}`), `mode_changed`
--- `{old, new}`. Any other name is a custom event for `winter.ev.emit`.
---
--- A reload drops every handler, command, keymap, timer and option the config
--- made, then runs init.lua again: register things unconditionally in init.lua
--- and use `ready` + `reload` for one-time setup that should survive it.
--- Returns an id for `winter.ev.off`.
---@overload fun(event: "buf_enter"|"buf_write_pre"|"buf_write_post", fn: fun(ev: winter.BufEvent), opts?: { once?: boolean }): integer
---@overload fun(event: "file_type", fn: fun(ev: winter.FileTypeEvent), opts?: { once?: boolean }): integer
---@overload fun(event: "buf_reload", fn: fun(ev: winter.BufReloadEvent), opts?: { once?: boolean }): integer
---@overload fun(event: "mode_changed", fn: fun(ev: winter.ModeEvent), opts?: { once?: boolean }): integer
---@param event string
---@param fn fun(ev: winter.Event)
---@param opts? { once?: boolean }
---@return integer id
function winter.ev.on(event, fn, opts) end

---@param id integer
---@return boolean removed
function winter.ev.off(id) end

---Fire a custom event. Arguments must be strings or numbers.
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
---`priority` (lowest is dropped first on a narrow terminal; 100+ never is).
---@class winter.StatusBlock: winter.Style
---@field text? string
---@field pad? boolean
---@field priority? integer
---@field [integer] winter.StatusPiece

---What a component returns: `nil`/`false`/`""` hides it, a string is one
---piece, a table is a block.
---@alias winter.StatusResult string|false|winter.StatusBlock

---Built-in component names: `mode` `file` `lines` `buffers` `diagnostics`
---`filetype` `indent` `position` `percent` `gap`.
---@alias winter.StatusName string
---| "mode" | "file" | "lines" | "buffers" | "diagnostics"
---| "filetype" | "indent" | "position" | "percent" | "gap"

---@alias winter.StatusFn fun(ctx: winter.StatusCtx): winter.StatusResult?

---@class winter.StatusSetup
---@field left? (winter.StatusName|winter.StatusFn)[] Component names or functions.
---@field right? (winter.StatusName|winter.StatusFn)[]
---Separators between blocks of different background: a preset, `false`
---(= `"none"`), or custom glyphs. `"auto"` = powerline arrows with nerdicons.
---@field separators? "powerline"|"round"|"slant"|"none"|"auto"|boolean|{ left?: string|boolean, right?: string|boolean }
---@field enabled? boolean Draw the bar's components at all.

---@class winter.statusline
winter.statusline = {}

---Set the layout. Only the keys you pass change. A function in `left`/`right`
---becomes an anonymous component.
---
---Colours come from theme groups: `ui.statusline` (the bar),
---`ui.statusline.<component>`, and `ui.statusline.<mode>` for the mode pill
---(`normal insert visual vline replace command search files`).
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

---Back to the default layout.
function winter.statusline.reset() end

--------------------------------------------------------------------------
-- Theme
--------------------------------------------------------------------------

---@class winter.theme
winter.theme = {}

---Style a group: a `ui.*` / `diag.*` name, or a tree-sitter capture such as
---`"comment"`. `false`/`nil` removes the override.
---@param group string
---@param style winter.Style|false|nil
function winter.theme.set(group, style) end

---Set many groups at once: `{ [group] = style | false }`.
---@param groups table<string, winter.Style|false>
function winter.theme.apply(groups) end

---The override of a group, or `nil` if none is set.
---@param group string
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

---Make group `from` look like `to`. `false` unlinks.
---@param from string
---@param to string|false
function winter.theme.link(from, to) end

--------------------------------------------------------------------------
-- UI: messages, input, pickers, floats
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
---@field at? "center"|"cursor"|"screen"
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

---nvim-style chooser. `on_choice` gets the chosen item (the value you passed,
---not a copy) and its index, or `nil` when cancelled.
---@generic T
---@param items T[]
---@param opts? { prompt?: string, format?: fun(item: T): string }
---@param on_choice fun(item: T|nil, index?: integer)
function winter.ui.select(items, opts, on_choice) end

---@class winter.PickerItem
---@field text string Shown and fuzzy matched.
---@field detail? string Shown dim to the right.
---@field path? string File for the preview pane.
---@field line? integer
---@field col? integer

---@class winter.PickerOpts
---@field prompt? string
---@field items? (string|winter.PickerItem)[]
---@field fuzzy? boolean Default true. `false`: you filter via `on_query`.
---@field preview? boolean Preview pane (items need `path`/`line`).
---@field paths? boolean Items are file paths: icon + dimmed directory.
---@field max_results? integer
---@field on_accept? fun(item: string|winter.PickerItem, index: integer)
---@field on_close? fun()
---@field on_query? fun(text: string) Answer with `winter.picker.set_items`.

---@class winter.picker
winter.picker = {}

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

---The built-in live grep picker.
function winter.picker.grep() end

---@class winter.FloatOpts
---@field at? "cursor"|"screen" Default `"cursor"`.
---@field x? integer
---@field y? integer
---@field w? integer Sized from `lines` when omitted.
---@field h? integer
---@field z? integer
---@field lines? string[]
---@field style? winter.Style
---@field border? boolean|winter.Border
---@field border_style? winter.Style
---@field cover_bars? boolean Draw over the statusline.
---@field dismiss_on_move? boolean Default true.
---@field esc_closes? boolean Default true.
---@field visible? boolean Default true.
---@field modal? boolean
---@field pass? string[] Single-character keys that fall through to the editor.
---@field cursor? integer[] `{x, y}` cursor position inside the float.
---@field on_key? fun(name: string, code: integer): boolean? Return true to consume.
---@field on_close? fun()
---@field on_paint? fun(id: integer, w: integer, h: integer)

---@class winter.float
winter.float = {}

---Open a floating window. Returns its id.
---@param opts winter.FloatOpts
---@return integer id
function winter.float.open(opts) end

---Draw text at `x, y` inside the float. Returns the end column.
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
---@param ns integer
---@param line1 integer
---@param col1 integer
---@param line2 integer
---@param col2 integer
---@param style? winter.DecoStyle
function winter.deco.span(ns, line1, col1, line2, col2, style) end

---Virtual text after the end of a line; it follows the line as text changes.
---`prio` orders items on one line (higher is leftmost; diagnostics are 99).
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
---@field on_stderr? fun(chunk: string)
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

---Call `fn` once after `ms` milliseconds. Returns a timer id.
---@param ms integer
---@param fn fun()
---@return integer id
function winter.timer.after(ms, fn) end

---Call `fn` every `ms` milliseconds until cancelled.
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

---Diagnostics of a buffer (default: current).
---@param bufnr? integer
---@return winter.Diagnostic[]
function winter.diag.get(bufnr) end

---@param bufnr? integer
---@return { error: integer, warn: integer, info: integer, hint: integer }
function winter.diag.count(bufnr) end

---Jump to the next (`dir > 0`) or previous diagnostic.
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
---@field ["goto"] fun(kind: "definition"|"declaration"|"type_definition"|"implementation"|"references") Jump to a symbol's definition, references, etc. via the language server.
winter.lsp = {}

---Configure (and enable) a server by name; tables merge deeply over earlier
---calls and over the lspconfig definition. The name `"*"` sets defaults for
---every server. Raises on a bad option.
---@param name string
---@param opts? winter.LspServer
function winter.lsp.setup(name, opts) end

---clangd: switch between source and header.
function winter.lsp.switch_source_header() end

function winter.lsp.code_action() end

---Rename the symbol under the cursor; `preset` pre-fills the prompt.
---@param preset? string
function winter.lsp.rename(preset) end

--------------------------------------------------------------------------
-- Formatting
--------------------------------------------------------------------------

---A formatter: reads the buffer on stdin, writes the result to stdout.
---`$FILENAME`, `$DIRNAME`, `$BASENAME` and `$EXT` are expanded in `cmd`/`cwd`.
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
---@field timeout? integer Same as `formattimeout`.
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
-- Snippets, languages, tree
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
