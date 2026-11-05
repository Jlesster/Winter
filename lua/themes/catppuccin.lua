-- ~/.config/winter/catppuccin.lua
-- Catppuccin for winter, ported from catppuccin/nvim (all four flavours).
--
--   require("catppuccin").setup({ flavour = "mocha" })
--
-- options (all optional):
--   flavour               "mocha" (default) | "macchiato" | "frappe" | "latte"
--   transparent_background  false to paint ui.normal / ui.float / ui.menu /
--                         ui.statusline backgrounds (default: transparent)
--   cursor                colour the cursor falls back to off-syntax
--                         (default: rosewater; ui.cursor's *fg* is what winter uses)
--   styles                per-group text styles, defaulting to the nvim
--                         catppuccin set (comments/conditionals/loops italic,
--                         functions/booleans/types bold, keywords bold+italic,
--                         properties/miscs italic)
--   overrides             table, or function(C, U) -> table, of winter.theme.apply
--                         entries applied last
--   rainbow               false to leave winter.theme.rainbow alone
--
-- Capture names are winter's (no "@"). Language-specific groups use a
-- ".<lang>" suffix, e.g. 'constructor.lua'; they need winter's lang-aware theme_for.

local M = {}

M.palettes = {
  latte = {
    rosewater = '#dc8a78',
    flamingo = '#dd7878',
    pink = '#ea76cb',
    mauve = '#8839ef',
    red = '#d20f39',
    maroon = '#e64553',
    peach = '#fe640b',
    yellow = '#df8e1d',
    green = '#40a02b',
    teal = '#179299',
    sky = '#04a5e5',
    sapphire = '#209fb5',
    blue = '#1e66f5',
    lavender = '#7287fd',
    text = '#4c4f69',
    subtext1 = '#5c5f77',
    subtext0 = '#6c6f85',
    overlay2 = '#7c7f93',
    overlay1 = '#8c8fa1',
    overlay0 = '#9ca0b0',
    surface2 = '#acb0be',
    surface1 = '#bcc0cc',
    surface0 = '#ccd0da',
    base = '#eff1f5',
    mantle = '#e6e9ef',
    crust = '#dce0e8',
  },
  frappe = {
    rosewater = '#f2d5cf',
    flamingo = '#eebebe',
    pink = '#f4b8e4',
    mauve = '#ca9ee6',
    red = '#e78284',
    maroon = '#ea999c',
    peach = '#ef9f76',
    yellow = '#e5c890',
    green = '#a6d189',
    teal = '#81c8be',
    sky = '#99d1db',
    sapphire = '#85c1dc',
    blue = '#8caaee',
    lavender = '#babbf1',
    text = '#c6d0f5',
    subtext1 = '#b5bfe2',
    subtext0 = '#a5adce',
    overlay2 = '#949cbb',
    overlay1 = '#838ba7',
    overlay0 = '#737994',
    surface2 = '#626880',
    surface1 = '#51576d',
    surface0 = '#414559',
    base = '#303446',
    mantle = '#292c3c',
    crust = '#232634',
  },
  macchiato = {
    rosewater = '#f4dbd6',
    flamingo = '#f0c6c6',
    pink = '#f5bde6',
    mauve = '#c6a0f6',
    red = '#ed8796',
    maroon = '#ee99a0',
    peach = '#f5a97f',
    yellow = '#eed49f',
    green = '#a6da95',
    teal = '#8bd5ca',
    sky = '#91d7e3',
    sapphire = '#7dc4e4',
    blue = '#8aadf4',
    lavender = '#b7bdf8',
    text = '#cad3f5',
    subtext1 = '#b8c0e0',
    subtext0 = '#a5adcb',
    overlay2 = '#939ab7',
    overlay1 = '#8087a2',
    overlay0 = '#6e738d',
    surface2 = '#5b6078',
    surface1 = '#494d64',
    surface0 = '#363a4f',
    base = '#24273a',
    mantle = '#1e2030',
    crust = '#181926',
  },
  mocha = {
    rosewater = '#f5e0dc',
    flamingo = '#f2cdcd',
    pink = '#f5c2e7',
    mauve = '#cba6f7',
    red = '#f38ba8',
    maroon = '#eba0ac',
    peach = '#fab387',
    yellow = '#f9e2af',
    green = '#a6e3a1',
    teal = '#94e2d5',
    sky = '#89dceb',
    sapphire = '#74c7ec',
    blue = '#89b4fa',
    lavender = '#b4befe',
    text = '#cdd6f4',
    subtext1 = '#bac2de',
    subtext0 = '#a6adc8',
    overlay2 = '#9399b2',
    overlay1 = '#7f849c',
    overlay0 = '#6c7086',
    surface2 = '#585b70',
    surface1 = '#45475a',
    surface0 = '#313244',
    base = '#1e1e2e',
    mantle = '#181825',
    crust = '#11111b',
  },
}

M.flavour = 'mocha'

-- ---------------------------------------------------------------- helpers
local function hex_to_rgb(h)
  return tonumber(h:sub(2, 3), 16),
    tonumber(h:sub(4, 5), 16),
    tonumber(h:sub(6, 7), 16)
end

-- alpha 1 = fg, 0 = bg
local function blend(fg, bg, alpha)
  local fr, fg_, fb = hex_to_rgb(fg)
  local br, bg_, bb = hex_to_rgb(bg)
  local function ch(a, b)
    return math.floor(math.min(math.max(0, alpha * a + (1 - alpha) * b), 255) + 0.5)
  end
  return string.format('#%02x%02x%02x', ch(fr, br), ch(fg_, bg_), ch(fb, bb))
end

M.util = {
  blend = blend,
  darken = function(hex, amount, bg) return blend(hex, bg, math.abs(amount)) end,
  lighten = function(hex, amount, fg) return blend(hex, fg, math.abs(amount)) end,
}
local U = M.util

-- {"bold","italic"} -> { bold = true, italic = true }
local function flags(list)
  local t = {}
  for _, k in ipairs(list or {}) do
    if k == 'strikethrough' or k == 'strike' then
      t.strikethrough = true
    elseif k == 'undercurl' then
      t.undercurl = true
    elseif k == 'underline' then
      t.underline = true
    elseif k == 'bold' or k == 'italic' then
      t[k] = true
    end
  end
  return t
end

local function with(base, list)
  for k, v in pairs(flags(list)) do
    base[k] = v
  end
  return base
end

-- ----------------------------------------------------------------- groups
local function build(C, O)
  local none = O.transparent_background
  local S = O.styles
  local latte = M.flavour == 'latte'

  local fn = function(c) return with({ fg = c }, S.functions) end
  local kw = function(c) return with({ fg = c }, S.keywords) end
  local str = function(c) return with({ fg = c }, S.strings) end
  local typ = function(c) return with({ fg = c }, S.types) end
  local misc = function(c) return with({ fg = c }, S.miscs) end

  local T = {
    -- ---------------------------------------------------- treesitter
    ['variable'] = with({ fg = C.text }, S.variables),
    ['variable.builtin'] = with({ fg = C.red }, S.properties),
    ['variable.parameter'] = { fg = C.maroon, italic = true },
    ['variable.member'] = { fg = C.lavender },

    ['constant'] = { fg = C.peach },
    ['constant.builtin'] = { fg = C.peach, bold = true },
    ['constant.macro'] = { fg = C.mauve },
    ['module'] = misc(C.yellow),
    ['label'] = { fg = C.sapphire },

    ['string'] = str(C.green),
    ['string.documentation'] = str(C.teal),
    ['string.regexp'] = str(C.pink),
    ['string.escape'] = str(C.pink),
    ['string.special'] = { fg = C.pink },
    ['string.special.path'] = { fg = C.pink },
    ['string.special.symbol'] = { fg = C.flamingo },
    ['string.special.url'] = { fg = C.blue, italic = true, underline = true },
    ['character'] = { fg = C.teal },
    ['character.special'] = { fg = C.pink },

    ['boolean'] = with({ fg = C.peach }, S.booleans),
    ['number'] = with({ fg = C.peach }, S.numbers),
    ['number.float'] = with({ fg = C.peach }, S.numbers),

    ['type'] = typ(C.yellow),
    ['type.builtin'] = { fg = C.yellow, bold = true, italic = true },
    ['type.definition'] = typ(C.yellow),
    ['attribute'] = { fg = C.peach },
    ['property'] = with({ fg = C.lavender }, S.properties),

    ['function'] = fn(C.blue),
    ['function.builtin'] = fn(C.peach),
    ['function.call'] = fn(C.blue),
    ['function.macro'] = fn(C.pink),
    ['function.method'] = fn(C.blue),
    ['function.method.call'] = fn(C.blue),
    ['constructor'] = { fg = C.yellow },
    ['operator'] = with({ fg = C.sky }, S.operators),

    ['keyword'] = kw(C.mauve),
    ['keyword.modifier'] = kw(C.mauve),
    ['keyword.type'] = kw(C.mauve),
    ['keyword.coroutine'] = kw(C.mauve),
    ['keyword.function'] = { fg = C.mauve, italic = true },
    ['keyword.operator'] = kw(C.mauve),
    ['keyword.import'] = { fg = C.mauve, italic = true },
    ['keyword.repeat'] = with({ fg = C.mauve }, S.loops),
    ['keyword.return'] = kw(C.mauve),
    ['keyword.debug'] = kw(C.mauve),
    ['keyword.exception'] = kw(C.mauve),
    ['keyword.conditional'] = with({ fg = C.mauve }, S.conditionals),
    ['keyword.conditional.ternary'] = with({ fg = C.sky }, S.operators),
    ['keyword.directive'] = { fg = C.pink },
    ['keyword.directive.define'] = { fg = C.pink },
    ['keyword.export'] = kw(C.mauve),

    ['punctuation'] = { fg = C.overlay2 },
    ['punctuation.delimiter'] = { fg = C.overlay2 },
    ['punctuation.delimiter.regex'] = str(C.pink),
    ['punctuation.bracket'] = { fg = C.overlay2 },
    ['punctuation.special'] = { fg = C.pink },

    ['comment'] = with({ fg = C.overlay2 }, S.comments),
    ['comment.documentation'] = with({ fg = C.overlay2 }, S.comments),
    ['comment.error'] = { fg = C.base, bg = C.red },
    ['comment.warning'] = { fg = C.base, bg = C.yellow },
    ['comment.hint'] = { fg = C.base, bg = C.blue },
    ['comment.note'] = { fg = C.base, bg = C.blue },
    ['comment.todo'] = { fg = C.base, bg = C.flamingo, bold = true, italic = true },

    ['markup'] = { fg = C.text },
    ['markup.strong'] = { fg = C.red, bold = true },
    ['markup.italic'] = { fg = C.red, italic = true },
    ['markup.strikethrough'] = { fg = C.text, strikethrough = true },
    ['markup.underline'] = { underline = true },
    ['markup.heading'] = { fg = C.blue },
    ['markup.math'] = { fg = C.blue },
    ['markup.quote'] = { fg = C.pink },
    ['markup.environment'] = { fg = C.pink },
    ['markup.environment.name'] = { fg = C.blue },
    ['markup.link'] = { fg = C.lavender },
    ['markup.link.label'] = { fg = C.lavender },
    ['markup.link.url'] = { fg = C.blue, italic = true, underline = true },
    ['markup.raw'] = { fg = C.green },
    ['markup.list'] = { fg = C.teal },
    ['markup.list.checked'] = { fg = C.green },
    ['markup.list.unchecked'] = { fg = C.overlay1 },
    ['markup.heading.markdown'] = { bold = true },
    ['markup.heading.1.markdown'] = { fg = C.red },
    ['markup.heading.2.markdown'] = { fg = C.peach },
    ['markup.heading.3.markdown'] = { fg = C.yellow },
    ['markup.heading.4.markdown'] = { fg = C.green },
    ['markup.heading.5.markdown'] = { fg = C.sapphire },
    ['markup.heading.6.markdown'] = { fg = C.lavender },

    -- language-specific (needs the theme_for(cap, lang) change)
    ['constructor.lua'] = { fg = C.overlay2 },
    ['keyword.import.c'] = { fg = C.yellow },
    ['keyword.import.cpp'] = { fg = C.yellow },
    ['constant.java'] = { fg = C.teal },

    -- bash
    ['function.builtin.bash'] = { fg = C.red, italic = true },
    ['variable.parameter.bash'] = { fg = C.green },

    -- css / scss
    ['property.css'] = { fg = C.blue },
    ['property.scss'] = { fg = C.blue },
    ['property.id.css'] = { fg = C.yellow },
    ['property.class.css'] = { fg = C.yellow },
    ['type.css'] = { fg = C.lavender },
    ['type.tag.css'] = { fg = C.blue },
    ['string.plain.css'] = { fg = C.text },
    ['number.css'] = { fg = C.peach },
    ['keyword.directive.css'] = kw(C.mauve),

    -- html
    ['string.special.url.html'] = { fg = C.green },
    ['markup.link.label.html'] = { fg = C.text },
    ['character.special.html'] = { fg = C.red },
    ['markup.heading.html'] = { fg = C.text },
    ['markup.heading.1.html'] = { fg = C.text },
    ['markup.heading.2.html'] = { fg = C.text },
    ['markup.heading.3.html'] = { fg = C.text },
    ['markup.heading.4.html'] = { fg = C.text },
    ['markup.heading.5.html'] = { fg = C.text },
    ['markup.heading.6.html'] = { fg = C.text },

    -- python
    ['constructor.python'] = { fg = C.sky },

    -- yaml
    ['label.yaml'] = { fg = C.yellow },

    -- ruby
    ['string.special.symbol.ruby'] = { fg = C.flamingo },

    -- php
    ['function.method.php'] = fn(C.blue),
    ['function.method.call.php'] = fn(C.blue),

    -- c#
    ['attribute.c_sharp'] = { fg = C.yellow },

    -- gitcommit / gitignore
    ['comment.warning.gitcommit'] = { fg = C.yellow },
    ['string.special.path.gitignore'] = { fg = C.text },

    ['diff.plus'] = { fg = C.green },
    ['diff.minus'] = { fg = C.red },
    ['diff.delta'] = { fg = C.blue },

    ['tag'] = { fg = C.blue },
    ['tag.builtin'] = { fg = C.blue },
    ['tag.attribute'] = misc(C.yellow),
    ['tag.delimiter'] = { fg = C.teal },

    ['error'] = { fg = C.red },

    -- ---------------------------------------------- semantic tokens
    -- unlisted lsp types fall back to the treesitter group (see winter's
    -- token_style); "variable" is deliberately left unstyled.
    ['lsp.enumMember'] = { fg = C.teal },
    ['lsp.function.defaultLibrary'] = fn(C.peach),
    ['lsp.function.builtin'] = fn(C.peach),
    ['lsp.macro'] = { fg = C.mauve },
    ['lsp.method.defaultLibrary'] = fn(C.peach),

    -- ------------------------------------------------------ editor ui
    ['ui.normal'] = { fg = C.text, bg = (not none) and C.base or nil },
    ['ui.message'] = { fg = C.text },
    ['ui.linenr'] = { fg = C.surface1 },
    ['ui.linenr.cursor'] = { fg = C.lavender },
    ['ui.tilde'] = { fg = C.surface1 },
    ['ui.ctrl'] = { fg = C.overlay0 },
    ['ui.welcome'] = { fg = C.overlay0 },
    ['ui.visual'] = { bg = C.surface1, bold = true },
    ['ui.search'] = { fg = C.text, bg = U.darken(C.sky, 0.30, C.base) },
    ['ui.search.current'] = { fg = C.mantle, bg = C.red },
    ['ui.search.cursor'] = { fg = C.rosewater },
    -- winter uses ui.cursor's FG as the cursor colour (nvim's Cursor.bg)
    ['ui.cursor'] = { fg = O.cursor or C.rosewater },
    ['ui.cursorline'] = {
      bg = latte and U.lighten(C.mantle, 0.70, C.base)
        or U.darken(C.surface0, 0.64, C.base),
    },
    ['ui.colorcolumn'] = { bg = C.surface0 },
    ['ui.matchparen'] = {
      fg = C.peach,
      bg = U.darken(C.surface1, 0.70, C.base),
      bold = true,
    },
    ['ui.trailing'] = { bg = U.darken(C.red, 0.30, C.base) },
    ['ui.snippet'] = { bg = C.surface0 },
    ['ui.snippet.active'] = { bg = C.surface1 },
    ['ui.reference'] = { bg = C.surface1 },
    ['ui.reference.write'] = { bg = C.surface1 },
    ['ui.subst.replace'] = { fg = C.pink, bg = C.surface1 },

    -- status line: catppuccin's lualine mode colours
    ['ui.statusline'] = { fg = C.text, bg = (not none) and C.mantle or nil },
    ['ui.statusline.normal'] = { fg = C.base, bg = C.blue, bold = true },
    ['ui.statusline.insert'] = { fg = C.base, bg = C.green, bold = true },
    ['ui.statusline.visual'] = { fg = C.base, bg = C.mauve, bold = true },
    ['ui.statusline.vline'] = { fg = C.base, bg = C.mauve, bold = true },
    ['ui.statusline.replace'] = { fg = C.base, bg = C.red, bold = true },
    ['ui.statusline.command'] = { fg = C.base, bg = C.peach, bold = true },
    ['ui.statusline.search'] = { fg = C.base, bg = C.yellow, bold = true },
    ['ui.statusline.files'] = { fg = C.base, bg = C.lavender, bold = true },

    -- floats / menus / input (Pmenu*, NormalFloat, FloatBorder)
    ['ui.float'] = { fg = C.text, bg = (not none) and C.mantle or nil },
    ['ui.float.border'] = { fg = C.cyan, bg = (not none) and C.mantle or nil },
    ['ui.float.title'] = { fg = C.crust, bg = C.lavender },
    ['ui.menu'] = { fg = C.overlay2, bg = (not none) and C.mantle or nil },
    ['ui.menu.select'] = { bg = C.surface0, bold = true },
    ['ui.menu.marker'] = { fg = C.flamingo },
    ['ui.menu.kind'] = { fg = C.blue },
    ['ui.menu.detail'] = { fg = C.overlay0 },
    ['ui.menu.doc'] = { fg = C.text },
    ['ui.menu.match'] = { fg = C.text, bold = true },
    ['ui.menu.key'] = { fg = C.lavender, bold = true },
    ['ui.menu.disabled'] = { fg = C.overlay0 },
    ['ui.input'] = { fg = C.text, bg = (not none) and C.mantle or nil },
    ['ui.input.prompt'] = { fg = C.blue, bold = true },
    ['ui.input.border'] = { fg = C.surface2, bg = (not none) and C.mantle or nil },

    -- picker (telescope integration)
    ['ui.picker.border'] = { fg = C.surface2 },
    ['ui.picker.prompt'] = { fg = C.flamingo },

    -- file tree (neo-tree integration)
    ['ui.tree.file'] = { fg = C.text },
    ['ui.tree.dir'] = { fg = C.blue },
    ['ui.tree.root'] = { fg = C.blue, bold = true },
    ['ui.tree.open'] = { fg = C.pink },
    ['ui.tree.active'] = { fg = C.pink },
    ['ui.tree.symlink'] = { fg = C.pink },
    ['ui.tree.hidden'] = { fg = C.overlay0 },
    ['ui.tree.guide'] = { fg = C.overlay0 },
    ['ui.tree.sep'] = { fg = C.surface1 },
    ['ui.tree.select'] = { fg = C.flamingo, bg = C.surface0, bold = true },
    ['ui.tree.match'] = { fg = C.green, bold = true },
    ['ui.tree.icon.dir'] = { fg = C.blue },
    ['ui.tree.icon.file'] = { fg = C.subtext1 },
    ['ui.tree.git.modified'] = { fg = C.yellow },
    ['ui.tree.git.added'] = { fg = C.green },
    ['ui.tree.git.deleted'] = { fg = C.red },
    ['ui.tree.git.untracked'] = { fg = C.mauve },
    ['ui.tree.git.conflict'] = { fg = C.red },
    ['ui.tree.git.staged'] = { fg = C.green },

    -- diagnostics (nvim: Error red, Warn yellow, Info sky, Hint teal)
    ['diag.error'] = { fg = C.red },
    ['diag.warn'] = { fg = C.yellow },
    ['diag.info'] = { fg = C.sky },
    ['diag.hint'] = { fg = C.teal },
    ['diag.virtual'] = { italic = true },
    ['diag.underline'] = { underline = true },
  }

  -- legacy / pre-0.10 capture names that older query sets still emit
  local alias = {
    parameter = 'variable.parameter',
    field = 'variable.member',
    namespace = 'module',
    method = 'function.method',
    ['method.call'] = 'function.method.call',
    float = 'number.float',
    symbol = 'string.special.symbol',
    ['string.regex'] = 'string.regexp',
    ['type.qualifier'] = 'keyword.modifier',
    storageclass = 'keyword.modifier',
    conditional = 'keyword.conditional',
    exception = 'keyword.exception',
    include = 'keyword.import',
    ['repeat'] = 'keyword.repeat',
    preproc = 'keyword.directive',
    define = 'keyword.directive.define',
    escape = 'string.escape',
    macro = 'constant.macro',
    ['text'] = 'markup',
    ['text.strong'] = 'markup.strong',
    ['text.emphasis'] = 'markup.italic',
    ['text.underline'] = 'markup.underline',
    ['text.strike'] = 'markup.strikethrough',
    ['text.uri'] = 'markup.link.url',
    ['text.math'] = 'markup.math',
    ['text.environment'] = 'markup.environment',
    ['text.environment.name'] = 'markup.environment.name',
    ['text.title'] = 'markup.heading',
    ['text.literal'] = 'markup.raw',
    ['text.reference'] = 'markup.link',
    ['text.todo'] = 'comment.todo',
    ['text.todo.checked'] = 'markup.list.checked',
    ['text.todo.unchecked'] = 'markup.list.unchecked',
    ['text.warning'] = 'comment.warning',
    ['text.note'] = 'comment.note',
    ['text.danger'] = 'comment.error',
    ['text.diff.add'] = 'diff.plus',
    ['text.diff.delete'] = 'diff.minus',
    ['text.title.1.markdown'] = 'markup.heading.1.markdown',
    ['text.title.2.markdown'] = 'markup.heading.2.markdown',
    ['text.title.3.markdown'] = 'markup.heading.3.markdown',
    ['text.title.4.markdown'] = 'markup.heading.4.markdown',
    ['text.title.5.markdown'] = 'markup.heading.5.markdown',
    ['text.title.6.markdown'] = 'markup.heading.6.markdown',
    ['method.php'] = 'function.method.php',
    ['method.call.php'] = 'function.method.call.php',
    ['symbol.ruby'] = 'string.special.symbol.ruby',
  }
  for old, new in pairs(alias) do
    if T[old] == nil and T[new] ~= nil then T[old] = T[new] end
  end
  return T
end

-- ------------------------------------------------------------------ setup
function M.setup(opts)
  opts = opts or {}
  local flavour = opts.flavour or 'mocha'
  local C = M.palettes[flavour]
  assert(C, "catppuccin: unknown flavour '" .. tostring(flavour) .. "'")
  M.flavour = flavour
  M.colors = C

  local O = {
    transparent_background = opts.transparent_background ~= false,
    cursor = opts.cursor,
  }
  local defaults = {
    comments = { 'italic' },
    conditionals = { 'italic' },
    loops = { 'italic' },
    functions = { 'bold' },
    keywords = { 'bold', 'italic' },
    booleans = { 'bold' },
    properties = { 'italic' },
    types = { 'bold' },
    miscs = { 'italic' },
  }
  O.styles = {}
  for _, k in ipairs({
    'comments',
    'conditionals',
    'loops',
    'functions',
    'keywords',
    'strings',
    'variables',
    'numbers',
    'booleans',
    'properties',
    'types',
    'operators',
    'miscs',
  }) do
    local given = opts.styles and opts.styles[k]
    O.styles[k] = given ~= nil and given or defaults[k] or {}
  end

  local T = build(C, O)

  -- an unset ui.normal bg would leave the old override's bg behind
  if O.transparent_background then T['ui.normal'].bg = nil end

  local extra = opts.overrides
  if type(extra) == 'function' then extra = extra(C, U) end
  if type(extra) == 'table' then
    for k, v in pairs(extra) do
      T[k] = v
    end
  end

  winter.theme.apply(T)
  -- semantic "variable" stays unstyled, like catppuccin
  winter.theme.apply({ ['lsp.variable'] = false })

  winter.theme.link('ui.hover', 'ui.float')
  winter.theme.link('ui.reference.read', 'ui.reference')

  if opts.rainbow ~= false then
    -- rainbow-delimiters order: red yellow blue orange green violet cyan
    winter.theme.rainbow({
      C.red,
      C.yellow,
      C.blue,
      C.peach,
      C.green,
      C.mauve,
      C.teal,
    })
  end
  return C
end

return M
