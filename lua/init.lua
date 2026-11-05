-- Winter example config
--
--   mkdir -p ~/.config/winter && cp init.example.lua ~/.config/winter/init.lua
--
-- or try it without touching your real config:
--
--   mkdir -p /tmp/wcfg/winter && cp init.example.lua /tmp/wcfg/winter/init.lua
--   XDG_CONFIG_HOME=/tmp/wcfg ./builddir/winter somefile.cpp
--
-- Saving any .lua file under the config dir hot-reloads it (`hotreload`), or
-- run `:reload`. A reload drops every handler, command, keymap, timer and
-- option this file made and runs it again from the top; `ready` does not
-- fire a second time, `reload` does (see section 5).
--
-- Everything below is optional and independent: delete what you don't want.
-- Run `:lua <expr>` inside Winter to poke at any of it live, e.g.
--   :lua winter.statusline.layout()
--   :lua winter.options.info("tabstop")

---------------------------------------------------------------------------
-- 0. Knobs
---------------------------------------------------------------------------

local cfg = {
  nerd = true, -- nerd-font icons + powerline statusline arrows
  theme = 'mocha', -- mocha | gruvbox | tokyonight  (see :Theme)
  trim_on_save = true, -- strip trailing whitespace before every write
  relnum = true, -- relative numbers, switched off while inserting
}

local function have(bin) -- is an executable on $PATH?
  return os.execute('command -v ' .. bin .. ' >/dev/null 2>&1') == true
end

---------------------------------------------------------------------------
-- 1. Options   (winter.opt.<name>; same names as `:set`, `:set name?` shows)
---------------------------------------------------------------------------

local o = winter.opt

o.tabstop = 4
o.shiftwidth = 4
o.expandtab = true
o.number = true
o.relativenumber = cfg.relnum
o.numberwidth = 4
o.scrolloff = 8
o.scrolleof = false
o.wrap = false
o.cursorline = true
o.matchparen = true
o.hltrailing = true
o.colorcolumn = 100
o.hlsearch = true
o.hlreference = true -- highlight other uses of the symbol (LSP)
o.rainbow = true -- rainbow delimiters
o.timeoutlen = 600 -- chord timeout (ms)
o.undofile = true
o.undolevels = 2000
o.history = 1000
o.remember_cursor = true
o.quit_times = 2
o.paste_indent = true
o.hotreload = true -- reload this config when a .lua file changes

-- C-family indent policy: 0 flat, 1 indent, 2 follow the block's first line
o.indentnamespace = 2
o.indentcase = 2
o.indent_engine = 0 -- 0 structural + ts fallback / 1 ts only / 2 structural only

o.border = 'rounded' -- rounded | straight | double | block
o.nerdicons = cfg.nerd
o.treeicons = cfg.nerd
o.treewidth = 32
o.treehidden = false
o.treefollow = true
o.treegit = true

-- diagnostics + semantic highlighting
o.diagnostics = true
o.diag_virtual_text = true
o.diag_undercurl = true
o.diag_severity = 4 -- 1 error .. 4 hint: show this and worse
o.semantic_tokens = true

-- formatting (the formatters themselves: section 9)
o.formatonsave = false
o.formattimeout = 1000 -- ms a formatter may block a save

-- cursor shape per mode: block | underline | bar, add -blink to blink
o.cursor = 'n:block; i:bar; v:underline; r:underline-blink'

---------------------------------------------------------------------------
-- 2. Themes   (winter.theme: ui.* groups + rainbow; syntax groups are the
--    tree-sitter capture names, e.g. "comment", "function")
--
-- Below: a small hand-rolled palette switcher. For a full port of the
-- catppuccin groups, drop catppuccin.lua next to this file and use
--   require("catppuccin").setup({ flavour = "mocha" })
-- instead of apply_theme().
---------------------------------------------------------------------------

local palettes = {
  mocha = {
    crust = '#11111b',
    mantle = '#181825',
    surface = '#313244',
    text = '#cdd6f4',
    sub = '#a6adc8',
    overlay = '#6c7086',
    blue = '#89b4fa',
    green = '#a6e3a1',
    mauve = '#cba6f7',
    red = '#f38ba8',
    peach = '#fab387',
    yellow = '#f9e2af',
    teal = '#94e2d5',
    lavender = '#b4befe',
  },
  gruvbox = {
    crust = '#1d2021',
    mantle = '#282828',
    surface = '#3c3836',
    text = '#ebdbb2',
    sub = '#a89984',
    overlay = '#7c6f64',
    blue = '#83a598',
    green = '#b8bb26',
    mauve = '#d3869b',
    red = '#fb4934',
    peach = '#fe8019',
    yellow = '#fabd2f',
    teal = '#8ec07c',
    lavender = '#d5c4a1',
  },
  tokyonight = {
    crust = '#16161e',
    mantle = '#1a1b26',
    surface = '#292e42',
    text = '#c0caf5',
    sub = '#9aa5ce',
    overlay = '#565f89',
    blue = '#7aa2f7',
    green = '#9ece6a',
    mauve = '#bb9af7',
    red = '#f7768e',
    peach = '#ff9e64',
    yellow = '#e0af68',
    teal = '#73daca',
    lavender = '#b4f9f8',
  },
}

local function apply_theme(name)
  local p = palettes[name]
  if not p then return false end
  local function pill(bg) return { fg = p.crust, bg = bg, bold = true } end

  winter.theme.reset() -- drop every override first
  winter.theme.apply({
    -- statusline: the bar, then one group per component / mode
    ['ui.statusline'] = { fg = p.sub, bg = p.mantle },
    ['ui.statusline.file'] = { fg = p.text, bg = p.surface },
    ['ui.statusline.modified'] = { fg = p.peach },
    ['ui.statusline.position'] = { fg = p.text, bg = p.surface },
    ['ui.statusline.normal'] = pill(p.blue),
    ['ui.statusline.insert'] = pill(p.green),
    ['ui.statusline.visual'] = pill(p.mauve),
    ['ui.statusline.vline'] = pill(p.mauve),
    ['ui.statusline.replace'] = pill(p.red),
    ['ui.statusline.command'] = pill(p.peach),
    ['ui.statusline.search'] = pill(p.yellow),
    ['ui.statusline.files'] = pill(p.teal),
    -- our own components below get their group by name too
    ['ui.statusline.branch'] = { fg = p.green, bg = p.surface },
    ['ui.statusline.selection'] = { fg = p.crust, bg = p.mauve, bold = true },

    -- pickers, menus, floats
    ['ui.picker.border'] = { fg = p.lavender },
    ['ui.picker.prompt'] = { fg = p.lavender, bold = true },
    ['ui.menu.select'] = { bg = p.surface, bold = true },
    ['ui.menu.marker'] = { fg = p.lavender },
    ['ui.menu.match'] = { fg = p.peach, bold = true },
    ['ui.menu.dir'] = { fg = p.overlay },
    ['ui.menu.detail'] = { fg = p.overlay },
    ['ui.float.border'] = { fg = p.overlay },
    ['ui.float.title'] = { fg = p.lavender, bold = true },

    ['comment'] = { italic = true },
  })
  winter.theme.rainbow({
    p.red,
    p.peach,
    p.yellow,
    p.green,
    p.teal,
    p.blue,
    p.mauve,
  })
  return true
end

apply_theme(cfg.theme)

---------------------------------------------------------------------------
-- 3. Statusline
--
--   winter.statusline.setup{ left=, right=, separators=, enabled= }
--   winter.statusline.component(name, fn(ctx) [, {priority=, style=}])
--
-- Built-ins: mode file lines buffers diagnostics filetype indent position
-- percent gap.  A component returns nil/false to hide, a string, or a table:
--   { "text", {"more", fg="#f38ba8"}, fg=, bg=, bold=, pad=false, priority=60 }
-- On a narrow terminal the lowest `priority` goes first (mode never does).
-- ctx = { mode, width, line, col, vcol, lines, name, lang, modified, bufnr,
--         bufs, icons, diag = {error, warn, info, hint} }
---------------------------------------------------------------------------

-- git branch: asked of git in the background, cached, refreshed on events
local branch = ''
local function refresh_branch()
  winter.job.run(
    'git rev-parse --abbrev-ref HEAD 2>/dev/null',
    function(out, code) branch = (code == 0) and (out:gsub('%s+$', '')) or '' end
  )
end

winter.statusline.component('branch', function(ctx)
  if branch == '' then return nil end
  local icon = ctx.icons and '\u{e0a0} ' or ''
  return icon .. branch
end, { priority = 55 })

-- visual selection size, only while selecting
winter.statusline.component('selection', function(ctx)
  if ctx.mode ~= 'visual' then return nil end
  local r = winter.selection()
  if not r then return nil end
  local n = r.line2 - r.line1 + 1
  return n > 1 and (n .. ' lines') or 'sel'
end, { priority = 90 })

-- a "line/total  col" ruler instead of the built-in position block
winter.statusline.component(
  'ruler',
  function(ctx) return string.format('%d/%d  %d', ctx.line, ctx.lines, ctx.vcol) end,
  { priority = 80 }
)

winter.statusline.setup({
  left = { 'mode', 'file', 'branch', 'selection', 'buffers' },
  right = {
    'diagnostics',
    'filetype',
    'indent',
    'ruler',
    'percent',
    -- a bare function works too (it becomes an anonymous component)
    function() return { os.date('%H:%M'), priority = 5 } end,
  },
  separators = cfg.nerd and 'powerline' or 'none',
})

---------------------------------------------------------------------------
-- 4. Per-language indentation
--    (options are global, so re-apply them whenever the buffer changes)
---------------------------------------------------------------------------

local indent = {
  lua = 2,
  yaml = 2,
  json = 2,
  javascript = 2,
  typescript = 2,
  html = 2,
  css = 2,
  nix = 2,
  toml = 2,
  markdown = 2,
  go = { width = 4, tabs = true },
  make = { width = 8, tabs = true },
}

local function apply_indent(lang)
  local rule = lang and indent[lang]
  local width, tabs = 4, false
  if type(rule) == 'number' then
    width = rule
  elseif type(rule) == 'table' then
    width, tabs = rule.width, rule.tabs
  end
  o.tabstop, o.shiftwidth, o.expandtab = width, width, not tabs
end

winter.ev.on('buf_enter', function()
  local info = winter.buf.info()
  apply_indent(info and info.lang)
end)
winter.ev.on('file_type', function(ev) -- {filename, lang}
  local info = winter.buf.info()
  if info and info.name == ev.filename then apply_indent(ev.lang) end
end)

---------------------------------------------------------------------------
-- 5. Events
--    ready (first load only), reload (after every hot reload / :reload),
--    quit, text_changed, cursor_moved (no args; they fire a lot, keep them
--    cheap), buf_enter/buf_write_pre/buf_write_post {filename},
--    buf_reload (file changed on disk; filename is ev[1]),
--    file_type {filename, lang}, mode_changed {old, new}
---------------------------------------------------------------------------

-- Timers die with a reload and `ready` does not fire again, so one-time
-- setup hangs off both events.
local function start()
  refresh_branch()
  winter.timer.every(30000, refresh_branch) -- also keeps the clock fresh
end

winter.ev.on('ready', start)
winter.ev.on('reload', start)
winter.ev.on('buf_reload', refresh_branch) -- e.g. after `git checkout`

winter.ev.on('buf_write_post', refresh_branch)

if cfg.trim_on_save then
  winter.ev.on('buf_write_pre', function()
    local info = winter.buf.info()
    if info and info.lang == 'markdown' then return end -- trailing "  " = <br>
    winter.cmd.run('Trim')
  end)
end

if cfg.relnum then -- absolute numbers in insert
  winter.ev.on(
    'mode_changed',
    function(ev) o.relativenumber = ev.new ~= 'insert' end
  )
end

---------------------------------------------------------------------------
-- 6. Commands   winter.cmd.add(Name, fn(args), opts)
--    opts: desc, nargs ("0" "1" "?" "*"), bang, range, aliases, complete
---------------------------------------------------------------------------

winter.cmd.add('Trim', function()
  local n = winter.buf.line_count()
  local lines, dirty = winter.buf.lines(1, n), false
  for i, l in ipairs(lines) do
    local t = l:gsub('%s+$', '')
    if t ~= l then
      lines[i], dirty = t, true
    end
  end
  if dirty then winter.buf.set_lines(1, n, lines) end -- one undo step
end, { desc = 'Strip trailing whitespace', nargs = '0', range = false })

winter.cmd.add('Theme', function(c)
  if not apply_theme(c.args) then winter.ui.status('unknown theme: ' .. c.args) end
end, {
  desc = 'Switch colour scheme',
  nargs = '1',
  complete = function(lead)
    local out = {}
    for name in pairs(palettes) do
      if name:find(lead, 1, true) == 1 then out[#out + 1] = name end
    end
    table.sort(out)
    return out
  end,
})

winter.cmd.add('Buffers', function()
  winter.ui.select(winter.buf.list(), {
    prompt = 'Buffers',
    format = function(b)
      return string.format(
        '%d  %s%s',
        b.index,
        b.name ~= '' and b.name or '[No Name]',
        b.modified and '  \u{25cf}' or ''
      )
    end,
  }, function(b)
    if b then winter.buf.switch(b.index) end
  end)
end, { desc = 'Pick a buffer', nargs = '0', range = false })

winter.cmd.add('Diag', function()
  local list = winter.diag.get()
  if #list == 0 then return winter.ui.status('no diagnostics') end
  winter.ui.select(list, {
    prompt = 'Diagnostics',
    format = function(d)
      return string.format(
        '%4d  %-5s %s',
        d.line,
        d.severity_name,
        d.message:gsub('\n.*', '')
      )
    end,
  }, function(d)
    if d then winter.cursor.set(d.line, d.col) end
  end)
end, { desc = 'Pick a diagnostic', nargs = '0', range = false })

winter.cmd.add('Todo', function()
  local items = {}
  for i, l in ipairs(winter.buf.lines(1, winter.buf.line_count())) do
    local tag = l:match('TODO') or l:match('FIXME') or l:match('HACK')
    if tag then items[#items + 1] = { line = i, text = l:gsub('^%s+', '') } end
  end
  if #items == 0 then return winter.ui.status('nothing to do') end
  winter.ui.select(items, {
    prompt = 'TODO',
    format = function(it) return string.format('%4d  %s', it.line, it.text) end,
  }, function(it)
    if it then winter.cursor.set(it.line, 1) end
  end)
end, { desc = 'List TODO/FIXME/HACK in this buffer', nargs = '0', range = false })

---------------------------------------------------------------------------
-- 7. Keymaps   winter.keymap.set(modes, keys, action_name | fn [, opts])
--    modes: letters from  n i r v x c s f   ("nv" = several), a word
--           ("normal"), or a table of either: { "n", "x" }
--    keys:  <C-x> <esc> <cr> <tab> <bs> <space> <lt>; plain chars otherwise
--    (no <leader>: <space> is used as one, wait `timeoutlen` for chords)
---------------------------------------------------------------------------

local map = winter.keymap.set
local cmd = winter.cmd.run

-- files / buffers / search
map('n', '<space>f', function() winter.picker.files() end)
map('n', '<space>g', function() winter.picker.grep() end)
map('n', '<space>b', function() cmd('Buffers') end)
map('n', '<space>e', 'tree.toggle')
map('n', '<space>h', function() cmd('nohlsearch') end)
map('n', ']b', function() winter.buf.cycle(1) end)
map('n', '[b', function() winter.buf.cycle(-1) end)
map('n', '<space><space>', function() winter.buf.alt() end)

-- writing / quitting
map('n', '<space>w', function() winter.buf.save() end)
map('n', '<space>W', function() winter.buf.save_all() end)
map('n', '<space>q', function() cmd('quit') end)

-- build
map('n', '<space>m', function() cmd('make') end)

-- LSP and diagnostics
map('n', 'gd', 'lsp.definition')
map('n', 'gr', 'lsp.references')
map('n', 'gy', 'lsp.type_definition')
map('n', 'gI', 'lsp.implementation')
map('n', 'gD', function() winter.lsp['goto']('declaration') end) -- `goto` is reserved
map('n', '<space>o', 'lsp.switch_source_header') -- clangd .h <-> .cpp
map('n', 'K', 'lsp.hover')
map('n', '<space>rn', 'lsp.rename')
map('n', '<space>ca', 'lsp.code_action')
map('n', '<space>cf', function() winter.format.run() end)
map('n', '<space>d', function() cmd('Diag') end)
map('n', ']d', function() winter.diag.jump(1) end)
map('n', '[d', function() winter.diag.jump(-1) end)
map('n', '<space>t', function() cmd('Todo') end)

-- toggles
map(
  'n',
  '<space>uw',
  function() print('wrap = ' .. tostring(winter.options.toggle('wrap'))) end
)
map(
  'n',
  '<space>un',
  function() print('number = ' .. tostring(winter.options.toggle('number'))) end
)
map(
  'n',
  '<space>uc',
  function() print('cursorline = ' .. tostring(winter.options.toggle('cursorline'))) end
)

-- user-defined operator / motion / text object (see winter.ext)
--   gS{motion}  sort the touched lines      (also: gSS, and in visual mode)
winter.ext.operator('sort_lines', function(r)
  local lines = winter.buf.lines(r.line1, r.line2)
  table.sort(lines)
  winter.buf.set_lines(r.line1, r.line2, lines)
end, 'gS')

--   gm  jump to the middle of the line
winter.ext.motion('middle', function(line)
  local text = winter.buf.line(line) or ''
  return line, math.floor(#text / 2) + 1, 'exclusive'
end, 'gm')

--   ie  the whole buffer:  dae, yie, vie ...
winter.ext.object('entire', function()
  local n = winter.buf.line_count()
  return 1, 1, n, #(winter.buf.line(n) or '') + 1
end, 'ie')

---------------------------------------------------------------------------
-- 8. Language servers
--    winter.lsp.setup(name, { cmd=, filetypes=, settings=, init_options=,
--                             enabled= })      winter.lsp.setup("*", {...})
--    sets defaults for every server. Calling setup also enables the server;
--    it starts when a buffer of one of its filetypes opens. Tables merge, so
--    a later call only needs the keys it changes.
--
--    To reuse nvim-lspconfig's definitions (read from <dir>/lsp/<name>.lua):
--      winter.lsp.setup("*", { lspconfig = "/path/to/nvim-lspconfig" })
--      winter.lsp.setup("clangd")           -- cmd/filetypes come from there
---------------------------------------------------------------------------

if have('clangd') then
  winter.lsp.setup('clangd', {
    cmd = {
      'clangd',
      '--background-index',
      '--clang-tidy',
      '--header-insertion=never',
    },
    filetypes = { 'c', 'cpp' },
  })
end

if have('rust-analyzer') then
  winter.lsp.setup('rust_analyzer', {
    cmd = { 'rust-analyzer' },
    filetypes = { 'rust' },
    settings = { ['rust-analyzer'] = { check = { command = 'clippy' } } },
  })
end

if have('lua-language-server') then
  winter.lsp.setup('lua_ls', {
    cmd = { 'lua-language-server' },
    filetypes = { 'lua' },
    settings = { Lua = { diagnostics = { globals = { 'winter' } } } },
  })
end

---------------------------------------------------------------------------
-- 9. Formatting
--    winter.format.setup{ on_save=, lsp_fallback=, timeout=, formatters=,
--                         by_ft= }            winter.format.run()  (= :format)
--    by_ft keys are a tree-sitter language or an extension (".h"); values
--    run in order, or just the first installed one with stop_after_first.
--    "lsp" is a valid name. `false` turns formatting off for that key.
--    Built-in formatters: stylua prettier prettierd clang-format rustfmt
--    gofmt goimports black shfmt google-java-format.
---------------------------------------------------------------------------

winter.format.setup({
  on_save = false, -- same as o.formatonsave
  lsp_fallback = true, -- no rule for the file: ask the server
  by_ft = {
    lua = { 'stylua' },
    cpp = { 'clang-format' },
    c = { 'clang-format' },
    rust = { 'rustfmt' },
    go = { 'goimports', 'gofmt' },
    javascript = { 'prettierd', 'prettier', stop_after_first = true },
    markdown = false, -- never format
  },
  formatters = {
    -- override or add: cmd reads stdin, writes stdout
    -- $FILENAME $DIRNAME $BASENAME $EXT are expanded
    shfmt = { cmd = { 'shfmt', '-i', '4', '-filename', '$FILENAME' } },
  },
})

---------------------------------------------------------------------------
-- 10. Languages, snippets, tree icons
---------------------------------------------------------------------------

winter.lang.comment('nix', '#')
winter.lang.comment('zig', '//')
winter.lang.comment('toml', '#')
winter.lang.comment('css', '/*', '*/') -- block comments: open, close

winter.snippet.add('todo', 'TODO(${1:me}): $0')
winter.snippet.add('main', 'int main(int argc, char** argv) {\n\t$0\n\treturn 0;\n}')
winter.snippet.add('once', '#pragma once\n$0')

winter.tree.icons({
  lua = '\u{e620}',
  nix = '\u{f313}',
  zig = '\u{e6a9}',
  toml = '\u{e6b2}',
})

---------------------------------------------------------------------------
-- 11. Decorations (winter.deco): virtual text that follows the line as you
--     edit.  Here: a "⚑" marker after every TODO line, refreshed on buffer
--     enter and after each save.
---------------------------------------------------------------------------

local ns = winter.deco.ns('todo-marks')

local function mark_todos()
  winter.deco.clear(ns)
  local n = winter.buf.line_count()
  if n > 5000 then return end -- keep text_changed cheap
  for i, l in ipairs(winter.buf.lines(1, n)) do
    if l:find('TODO') or l:find('FIXME') then
      winter.deco.eol(ns, i, '  \u{2691} todo', { fg = '#f9e2af', italic = true })
    end
  end
end

winter.ev.on('buf_enter', mark_todos)
winter.ev.on('buf_write_post', mark_todos)
