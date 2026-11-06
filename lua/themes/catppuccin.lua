--   require("catppuccin").setup({ flavour = "mocha" })
--
-- How it is built
--   The group tables below (editor, lsp, syntax, semantic_tokens, treesitter,
--   neotree, telescope, rainbow_delimiters, lualine) are catppuccin/nvim's own
--   sources, unchanged. They are merged in the same order and with the same
--   rules as upstream's mapper/compiler (`highlight_overrides` > integrations
--   > syntax > editor, `style` lists, no_italic/no_bold/no_underline, link
--   removal), and the result is then projected onto winter's groups:
--
--     @capture / @lsp.*       ->  winter capture / lsp.* semantic-token groups
--     Normal, CursorLine, ... ->  ui.*   (table `UI` below)
--     Diagnostic*             ->  diag.*
--     NeoTree*                ->  ui.tree.*       Telescope*  -> ui.picker.*
--     lualine theme           ->  ui.statusline.* RainbowDelimiter* -> rainbow
--
--   Links are resolved while projecting, so overriding a legacy group such as
--   `Function` still restyles every capture that links to it, as in nvim.
--
-- Options (same names and defaults as upstream; "-" = not applicable here)
--   flavour                 "latte" | "frappe" | "macchiato" | "mocha" | "auto"
--                           ("auto" = background.dark; winter has no light/dark)
--   background              { light = "latte", dark = "mocha" }
--   transparent_background  false  paint Normal/float/menu/statusline backgrounds
--   float                   { transparent = false, solid = false }
--   no_italic / no_bold / no_underline
--   styles                  comments, conditionals (italic), loops, functions,
--                           keywords, strings, variables, numbers, booleans,
--                           properties, types, operators
--   lsp_styles              virtual_text / underlines per severity
--   color_overrides         { all = {...}, mocha = {...} } palette overrides
--   highlight_overrides     { all = fn(C)|{...}, mocha = fn(C)|{...} }; keys are
--                           nvim names ("Comment", "@function") or winter
--                           groups ("ui.cursorline", "diag.virtual", "lsp.macro")
--   dim_inactive, term_colors, kitty, integrations, compile_path
--                           -  (no inactive windows / terminal palette / plugins)
-- winter-only
--   cursor                  palette name or "#rrggbb" for ui.cursor (default: the
--                           nvim Cursor bg, rosewater)
--   rainbow                 false to leave winter.theme.rainbow alone
--   overrides               table | fn(C, U) of winter.theme.apply entries,
--                           applied last (pre-1:1 API, kept for old configs)
--
-- API: setup(opts) -> C, load(flavour), groups(flavour, opts) -> table (pure),
-- palettes, flavours, options, flavour, util. `:Catppuccin <flavour>` switches.
-- setup() starts with winter.theme.reset() (upstream's `hi clear`), so set your
-- own winter.theme overrides after it, or pass them as highlight_overrides.
--
-- Assumptions standing in for nvim state: winblend = pumblend = 0.
-- Known engine limits (no way to express upstream's look) are listed in
-- docs at the bottom of this file.

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

M.flavours = { latte = 1, frappe = 2, macchiato = 3, mocha = 4 }

M.default_options = {
  flavour = 'auto',
  background = { light = 'latte', dark = 'mocha' },
  transparent_background = false,
  float = { transparent = false, solid = false },
  term_colors = false,
  dim_inactive = { enabled = false, shade = 'dark', percentage = 0.15 },
  no_italic = false,
  no_bold = false,
  no_underline = false,
  styles = {
    comments = { 'italic' },
    conditionals = { 'italic' },
    loops = {},
    functions = {},
    keywords = {},
    strings = {},
    variables = {},
    numbers = {},
    booleans = {},
    properties = {},
    types = {},
    operators = {},
  },
  lsp_styles = {
    virtual_text = {
      errors = { 'italic' },
      hints = { 'italic' },
      warnings = { 'italic' },
      information = { 'italic' },
      ok = { 'italic' },
    },
    underlines = {
      errors = { 'underline' },
      hints = { 'underline' },
      warnings = { 'underline' },
      information = { 'underline' },
      ok = { 'underline' },
    },
    inlay_hints = { background = true },
  },
  color_overrides = {},
  highlight_overrides = {},
  -- winter only
  cursor = nil,
  rainbow = true,
  overrides = nil,
}

---------------------------------------------------------------------------
-- colour utilities (catppuccin.utils.colors, the parts the groups use)
---------------------------------------------------------------------------

local U = { bg = '#000000', fg = '#ffffff' }
M.util = U

local function hex_to_rgb(hex_str)
  local hex = '[abcdef0-9][abcdef0-9]'
  local pat = '^#(' .. hex .. ')(' .. hex .. ')(' .. hex .. ')$'
  hex_str = string.lower(hex_str)
  assert(
    string.find(hex_str, pat) ~= nil,
    'hex_to_rgb: invalid hex_str: ' .. tostring(hex_str)
  )
  local red, green, blue = string.match(hex_str, pat)
  return { tonumber(red, 16), tonumber(green, 16), tonumber(blue, 16) }
end

---@param fg string
---@param bg string
---@param alpha number 0 gives bg, 1 gives fg
function U.blend(fg, bg, alpha)
  bg = hex_to_rgb(bg)
  fg = hex_to_rgb(fg)
  local blendChannel = function(i)
    local ret = (alpha * fg[i] + ((1 - alpha) * bg[i]))
    return math.floor(math.min(math.max(0, ret), 255) + 0.5)
  end
  return string.format(
    '#%02X%02X%02X',
    blendChannel(1),
    blendChannel(2),
    blendChannel(3)
  )
end

function U.darken(hex, amount, bg) return U.blend(hex, bg or U.bg, math.abs(amount)) end

function U.lighten(hex, amount, fg) return U.blend(hex, fg or U.fg, math.abs(amount)) end

function U.vary_color(palettes, default)
  if palettes[M.flavour] ~= nil then return palettes[M.flavour] end
  return default
end

---------------------------------------------------------------------------
-- nvim stand-ins, so the upstream group bodies below stay byte-identical
---------------------------------------------------------------------------

local vim = {
  o = { winblend = 0, pumblend = 0, pumborder = '' },
  fn = { has = function() return 0 end },
}

-- vim.tbl_deep_extend: lists are replaced, never merged (nvim >= 0.10)
local function islist(t)
  local n = 0
  for _ in pairs(t) do
    n = n + 1
  end
  for i = 1, n do
    if t[i] == nil then return false end
  end
  return true
end
local function can_merge(v)
  return type(v) == 'table' and (next(v) == nil or not islist(v))
end

-- vim.tbl_deep_extend("keep", ...): the first table wins
local function deep_keep(...)
  local ret = {}
  for i = 1, select('#', ...) do
    local t = select(i, ...)
    for k, v in pairs(t) do
      if ret[k] == nil then
        if can_merge(v) then
          ret[k] = deep_keep(v)
        else
          ret[k] = v
        end
      elseif can_merge(v) and can_merge(ret[k]) then
        ret[k] = deep_keep(ret[k], v)
      end
    end
  end
  return ret
end

-- vim.tbl_deep_extend("force", ...): the last table wins
local function deep_force(a, b) return deep_keep(b, a) end

---------------------------------------------------------------------------
-- winter's built-in capture colours (syntax.cpp kTheme). They are mocha
-- constants; the port states every one of them (see build(), step 3).
---------------------------------------------------------------------------

local ENGINE_CAPTURES = {
  'keyword',
  'conditional',
  'repeat',
  'include',
  'exception',
  'storageclass',
  'preproc',
  'define',
  'string',
  'character',
  'string.escape',
  'escape',
  'number',
  'float',
  'boolean',
  'constant',
  'function',
  'method',
  'function.builtin',
  'constructor',
  'type',
  'attribute',
  'namespace',
  'module',
  'property',
  'field',
  'variable.parameter',
  'parameter',
  'variable.builtin',
  'label',
  'operator',
  'punctuation',
  'comment',
  'variable.member',
  'keyword.directive',
  'function.macro',
  'string.regexp',
  'string.special',
  'string.special.url',
  'character.special',
  'comment.error',
  'comment.warning',
  'comment.todo',
  'comment.note',
  'tag',
  'tag.attribute',
  'tag.delimiter',
  'markup.heading',
  'markup.strong',
  'markup.italic',
  'markup.strikethrough',
  'markup.link',
  'markup.link.url',
  'markup.raw',
  'markup.list',
  'markup.quote',
  'diff.plus',
  'diff.minus',
  'diff.delta',
}

---------------------------------------------------------------------------
-- catppuccin/nvim group tables. Bodies are upstream's, unchanged; only the
-- nvim-version guards and the module boilerplate are dropped.
---------------------------------------------------------------------------

local G = {}

-- upstream: lua/catppuccin/groups/editor.lua
G.editor = function(C, O, U)
  local pumsolid = O.float.solid
  if vim.fn.has('nvim-0.12') == 1 then pumsolid = vim.o.pumborder == 'solid' end

  return {
    ColorColumn = { bg = C.surface0 }, -- used for the columns set with 'colorcolumn'
    Conceal = { fg = C.overlay1 }, -- placeholder characters substituted for concealed text (see 'conceallevel')
    Cursor = { fg = C.base, bg = C.rosewater }, -- character under the cursor
    lCursor = { fg = C.base, bg = C.rosewater }, -- the character under the cursor when |language-mapping| is used (see 'guicursor')
    CursorIM = { fg = C.base, bg = C.rosewater }, -- like Cursor, but used when in IME mode |CursorIM|
    CursorColumn = { bg = C.mantle }, -- Screen-column at the cursor, when 'cursorcolumn' is set.
    CursorLine = {
      bg = U.vary_color(
        { latte = U.lighten(C.mantle, 0.70, C.base) },
        U.darken(C.surface0, 0.64, C.base)
      ),
    }, -- Screen-line at the cursor, when 'cursorline' is set.  Low-priority if forecrust (ctermfg OR guifg) is not set.
    Dimmed = { fg = C.overlay1 },
    Directory = { fg = C.blue }, -- directory names (and other special names in listings)
    EndOfBuffer = { fg = C.surface1 }, -- filler lines (~) after the end of the buffer. By default, this is highlighted like |hl-NonText|.
    ErrorMsg = { fg = C.red, style = { 'bold', 'italic' } }, -- error messages on the command line
    VertSplit = { fg = O.transparent_background and C.surface1 or C.crust }, -- the column separating vertically split windows
    Folded = { fg = C.blue, bg = O.transparent_background and C.none or C.surface1 }, -- line used for closed folds
    FoldColumn = { fg = C.overlay0 }, -- 'foldcolumn'
    SignColumn = { fg = C.surface1 }, -- column where |signs| are displayed
    SignColumnSB = { bg = C.crust, fg = C.surface1 }, -- column where |signs| are displayed
    Substitute = { bg = C.surface1, fg = U.vary_color({ latte = C.red }, C.pink) }, -- |:substitute| replacement text highlighting
    LineNr = { fg = C.surface1 }, -- Line number for ":number" and ":#" commands, and when 'number' or 'relativenumber' option is set.
    CursorLineNr = { fg = C.lavender }, -- Like LineNr when 'cursorline' or 'relativenumber' is set for the cursor line. highlights the number in numberline.
    MatchParen = {
      fg = C.peach,
      bg = U.darken(C.surface1, 0.70, C.base),
      style = { 'bold' },
    }, -- The character under the cursor or just before it, if it is a paired bracket, and its match. |pi_paren.txt|
    ModeMsg = { fg = C.text, style = { 'bold' } }, -- 'showmode' message (e.g., "-- INSERT -- ")
    -- MsgArea = { fg = C.text }, -- Area for messages and cmdline, don't set this highlight because of https://github.com/neovim/neovim/issues/17832
    MsgSeparator = { link = 'WinSeparator' }, -- Separator for scrolled messages, `msgsep` flag of 'display'
    MoreMsg = { fg = C.blue }, -- |more-prompt|
    NonText = { fg = C.overlay0 }, -- '@' at the end of the window, characters from 'showbreak' and other characters that do not really exist in the text (e.g., ">" displayed when a double-wide character doesn't fit at the end of the line). See also |hl-EndOfBuffer|.
    Normal = { fg = C.text, bg = O.transparent_background and C.none or C.base }, -- normal text
    NormalNC = {
      fg = C.text,
      bg = (O.transparent_background and O.dim_inactive.enabled and C.dim)
        or (O.dim_inactive.enabled and C.dim)
        or (O.transparent_background and C.none)
        or C.base,
    }, -- normal text in non-current windows
    NormalSB = { fg = C.text, bg = C.crust }, -- normal text in non-current windows
    NormalFloat = {
      fg = C.text,
      bg = (O.float.transparent and vim.o.winblend == 0) and C.none or C.mantle,
    }, -- Normal text in floating windows.
    FloatBorder = O.float.solid
        and ((O.float.transparent and vim.o.winblend == 0) and {
          fg = C.surface2,
          bg = C.none,
        } or {
          fg = C.mantle,
          bg = C.mantle,
        })
      or {
        fg = C.blue,
        bg = (O.float.transparent and vim.o.winblend == 0) and C.none or C.mantle,
      },
    FloatTitle = O.float.solid and {
      fg = C.crust,
      bg = C.lavender,
    } or {
      fg = C.subtext0,
      bg = (O.float.transparent and vim.o.winblend == 0) and C.none or C.mantle,
    }, -- Title of floating windows
    FloatShadow = {
      bg = (O.float.transparent and vim.o.winblend == 0) and C.none or C.overlay0,
      blend = 80,
    },
    FloatShadowThrough = {
      bg = (O.float.transparent and vim.o.winblend == 0) and C.none or C.overlay0,
      blend = 100,
    },
    OkMsg = { fg = C.green }, -- success messages on the command line
    Pmenu = {
      bg = (O.transparent_background and vim.o.pumblend == 0) and C.none or C.mantle,
      fg = C.overlay2,
    }, -- Popup menu: normal item.
    PmenuBorder = {
      bg = (O.transparent_background and vim.o.pumblend == 0) and C.none or C.mantle,
      fg = pumsolid
          and ((O.transparent_background and vim.o.pumblend == 0) and C.none or C.mantle)
        or C.blue,
    }, -- Popup menu: border
    PmenuSel = { bg = C.surface0, style = { 'bold' } }, -- Popup menu: selected item.
    PmenuMatch = { fg = C.text, style = { 'bold' } }, -- Popup menu: matching text.
    PmenuMatchSel = { style = { 'bold' } }, -- Popup menu: matching text in selected item; is combined with |hl-PmenuMatch| and |hl-PmenuSel|.
    PmenuSbar = { bg = C.surface0 }, -- Popup menu: scrollbar.
    PmenuThumb = { bg = C.overlay0 }, -- Popup menu: Thumb of the scrollbar.
    PmenuKind = {
      bg = (O.transparent_background and vim.o.pumblend == 0) and C.none or C.mantle,
      fg = C.blue,
    }, -- Popup menu: item kind. Background and style must match `Pmenu`.
    PmenuKindSel = { bg = C.surface0, fg = C.blue, style = { 'bold' } }, -- Popup menu: selected item kind. Background and style must match `PmenuSel`.
    PmenuExtra = { fg = C.overlay0 }, -- Popup menu: normal item extra text.
    PmenuExtraSel = {
      bg = C.surface0,
      fg = C.overlay0,
      style = { 'bold' },
    }, -- Popup menu: selected item extra text.
    ComplMatchIns = { link = 'PreInsert' }, -- Matched text of the currently inserted completion.
    PreInsert = { fg = C.overlay2 }, -- Text inserted when "preinsert" is in 'completeopt'.
    ComplHint = { fg = C.subtext0 }, -- Virtual text of the currently selected completion.
    ComplHintMore = { link = 'Question' }, -- The additional information of the virtual text.
    Question = { fg = C.blue }, -- |hit-enter| prompt and yes/no questions
    QuickFixLine = { bg = U.darken(C.surface1, 0.70, C.base), style = { 'bold' } }, -- Current |quickfix| item in the quickfix window. Combined with |hl-CursorLine| when the cursor is there.
    Search = { bg = U.darken(C.sky, 0.30, C.base), fg = C.text }, -- Last search pattern highlighting (see 'hlsearch').  Also used for similar items that need to stand out.
    IncSearch = { bg = U.darken(C.sky, 0.90, C.base), fg = C.mantle }, -- 'incsearch' highlighting; also used for the text replaced with ":s///c"
    CurSearch = { bg = C.red, fg = C.mantle }, -- 'cursearch' highlighting: highlights the current search you're on differently
    SpecialKey = { link = 'NonText' }, -- Unprintable characters: text displayed differently from what it really is.  But not 'listchars' textspace. |hl-Whitespace|
    SpellBad = { sp = C.red, style = { 'undercurl' } }, -- Word that is not recognized by the spellchecker. |spell| Combined with the highlighting used otherwise.
    SpellCap = { sp = C.yellow, style = { 'undercurl' } }, -- Word that should start with a capital. |spell| Combined with the highlighting used otherwise.
    SpellLocal = { sp = C.blue, style = { 'undercurl' } }, -- Word that is recognized by the spellchecker as one that is used in another region. |spell| Combined with the highlighting used otherwise.
    SpellRare = { sp = C.green, style = { 'undercurl' } }, -- Word that is recognized by the spellchecker as one that is hardly ever used.  |spell| Combined with the highlighting used otherwise.
    StatusLine = {
      fg = C.text,
      bg = O.transparent_background and C.none or C.mantle,
    }, -- status line of current window
    StatusLineNC = {
      fg = C.surface1,
      bg = O.transparent_background and C.none or C.mantle,
    }, -- status lines of not-current windows Note: if this is equal to "StatusLine" Vim will use "^^^" in the status line of the current window.
    TabLine = { bg = C.crust, fg = C.overlay0 }, -- tab pages line, not active tab page label
    TabLineFill = { bg = O.transparent_background and C.none or C.mantle }, -- tab pages line, where there are no labels
    TabLineSel = { link = 'Normal' }, -- tab pages line, active tab page label
    TermCursor = { fg = C.base, bg = C.rosewater }, -- cursor in a focused terminal
    TermCursorNC = { fg = C.base, bg = C.overlay2 }, -- cursor in unfocused terminals
    Title = { fg = C.blue, style = { 'bold' } }, -- titles for output from ":set all", ":autocmd" etc.
    Visual = { bg = C.surface1, style = { 'bold' } }, -- Visual mode selection
    VisualNOS = { bg = C.surface1, style = { 'bold' } }, -- Visual mode selection when vim is "Not Owning the Selection".
    WarningMsg = { fg = C.yellow }, -- warning messages
    Whitespace = { fg = C.surface1 }, -- "nbsp", "space", "tab" and "trail" in 'listchars'
    WildMenu = { bg = C.overlay0 }, -- current match in 'wildmenu' completion
    WinBar = { fg = C.rosewater },
    WinBarNC = { link = 'WinBar' },
    WinSeparator = { fg = O.transparent_background and C.surface1 or C.crust },
  }
end

-- upstream: lua/catppuccin/groups/lsp.lua
G.lsp = function(C, O, U)
  local virtual_text = O.lsp_styles.virtual_text
  local underlines = O.lsp_styles.underlines
  local inlay_hints = O.lsp_styles.inlay_hints

  local error = C.red
  local warning = C.yellow
  local info = C.sky
  local hint = C.teal
  local ok = C.green
  local darkening_percentage = 0.095

  return {
    -- These groups are for the native LSP client. Some other LSP clients may
    -- use these groups, or use their own. Consult your LSP client's
    -- documentation.
    LspReferenceText = { bg = C.surface1 }, -- used for highlighting "text" references
    LspReferenceRead = { bg = C.surface1 }, -- used for highlighting "read" references
    LspReferenceWrite = { bg = C.surface1 }, -- used for highlighting "write" references
    -- highlight diagnostics in numberline

    DiagnosticVirtualTextError = {
      bg = O.transparent_background and C.none
        or U.darken(error, darkening_percentage, C.base),
      fg = error,
      style = virtual_text.errors,
    }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticVirtualTextWarn = {
      bg = O.transparent_background and C.none
        or U.darken(warning, darkening_percentage, C.base),
      fg = warning,
      style = virtual_text.warnings,
    }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticVirtualTextInfo = {
      bg = O.transparent_background and C.none
        or U.darken(info, darkening_percentage, C.base),
      fg = info,
      style = virtual_text.information,
    }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticVirtualTextHint = {
      bg = O.transparent_background and C.none
        or U.darken(hint, darkening_percentage, C.base),
      fg = hint,
      style = virtual_text.hints,
    }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticVirtualTextOk = {
      bg = O.transparent_background and C.none
        or U.darken(hint, darkening_percentage, C.base),
      fg = ok,
      style = virtual_text.ok,
    }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default

    DiagnosticError = { bg = C.none, fg = error, style = virtual_text.errors }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticWarn = { bg = C.none, fg = warning, style = virtual_text.warnings }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticInfo = { bg = C.none, fg = info, style = virtual_text.information }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticHint = { bg = C.none, fg = hint, style = virtual_text.hints }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default
    DiagnosticOk = { bg = C.none, fg = ok, style = virtual_text.ok }, -- Used as the mantle highlight group. Other Diagnostic highlights link to this by default

    DiagnosticUnderlineError = { style = underlines.errors, sp = error }, -- Used to underline "Error" diagnostics
    DiagnosticUnderlineWarn = { style = underlines.warnings, sp = warning }, -- Used to underline "Warn" diagnostics
    DiagnosticUnderlineInfo = { style = underlines.information, sp = info }, -- Used to underline "Info" diagnostics
    DiagnosticUnderlineHint = { style = underlines.hints, sp = hint }, -- Used to underline "Hint" diagnostics
    DiagnosticUnderlineOk = { style = underlines.ok, sp = ok }, -- Used to underline "Ok" diagnostics

    DiagnosticFloatingError = { fg = error }, -- Used to color "Error" diagnostic messages in diagnostics float
    DiagnosticFloatingWarn = { fg = warning }, -- Used to color "Warn" diagnostic messages in diagnostics float
    DiagnosticFloatingInfo = { fg = info }, -- Used to color "Info" diagnostic messages in diagnostics float
    DiagnosticFloatingHint = { fg = hint }, -- Used to color "Hint" diagnostic messages in diagnostics float
    DiagnosticFloatingOk = { fg = ok }, -- Used to color "Ok" diagnostic messages in diagnostics float

    DiagnosticSignError = { fg = error }, -- Used for "Error" signs in sign column
    DiagnosticSignWarn = { fg = warning }, -- Used for "Warn" signs in sign column
    DiagnosticSignInfo = { fg = info }, -- Used for "Info" signs in sign column
    DiagnosticSignHint = { fg = hint }, -- Used for "Hint" signs in sign column
    DiagnosticSignOk = { fg = ok }, -- Used for "Ok" signs in sign column

    LspDiagnosticsDefaultError = { fg = error }, -- Used as the mantle highlight group. Other LspDiagnostic highlights link to this by default (except Underline)
    LspDiagnosticsDefaultWarning = { fg = warning }, -- Used as the mantle highlight group. Other LspDiagnostic highlights link to this by default (except Underline)
    LspDiagnosticsDefaultInformation = { fg = info }, -- Used as the mantle highlight group. Other LspDiagnostic highlights link to this by default (except Underline)
    LspDiagnosticsDefaultHint = { fg = hint }, -- Used as the mantle highlight group. Other LspDiagnostic highlights link to this by default (except Underline)
    LspSignatureActiveParameter = { bg = C.surface0, style = { 'bold' } },
    -- LspDiagnosticsFloatingError         = { }, -- Used to color "Error" diagnostic messages in diagnostics float
    -- LspDiagnosticsFloatingWarning       = { }, -- Used to color "Warning" diagnostic messages in diagnostics float
    -- LspDiagnosticsFloatingInformation   = { }, -- Used to color "Information" diagnostic messages in diagnostics float
    -- LspDiagnosticsFloatingHint          = { }, -- Used to color "Hint" diagnostic messages in diagnostics float

    LspDiagnosticsError = { fg = error },
    LspDiagnosticsWarning = { fg = warning },
    LspDiagnosticsInformation = { fg = info },
    LspDiagnosticsHint = { fg = hint },
    LspDiagnosticsVirtualTextError = { fg = error, style = virtual_text.errors }, -- Used for "Error" diagnostic virtual text
    LspDiagnosticsVirtualTextWarning = {
      fg = warning,
      style = virtual_text.warnings,
    }, -- Used for "Warning" diagnostic virtual text
    LspDiagnosticsVirtualTextInformation = {
      fg = info,
      style = virtual_text.warnings,
    }, -- Used for "Information" diagnostic virtual text
    LspDiagnosticsVirtualTextHint = { fg = hint, style = virtual_text.hints }, -- Used for "Hint" diagnostic virtual text
    LspDiagnosticsUnderlineError = { style = underlines.errors, sp = error }, -- Used to underline "Error" diagnostics
    LspDiagnosticsUnderlineWarning = { style = underlines.warnings, sp = warning }, -- Used to underline "Warning" diagnostics
    LspDiagnosticsUnderlineInformation = {
      style = underlines.information,
      sp = info,
    }, -- Used to underline "Information" diagnostics
    LspDiagnosticsUnderlineHint = { style = underlines.hints, sp = hint }, -- Used to underline "Hint" diagnostics
    LspCodeLens = { fg = C.overlay0 }, -- virtual text of the codelens
    LspCodeLensSeparator = { link = 'LspCodeLens' }, -- virtual text of the codelens separators
    LspInlayHint = {
      -- fg of `Comment`
      fg = C.overlay0,
      -- bg of `CursorLine`
      bg = (O.transparent_background or not inlay_hints.background) and C.none
        or U.vary_color(
          { latte = U.lighten(C.mantle, 0.70, C.base) },
          U.darken(C.surface0, 0.64, C.base)
        ),
    }, -- virtual text of the inlay hints
    LspInfoBorder = { link = 'FloatBorder' }, -- LspInfo border
  }
end

-- upstream: lua/catppuccin/groups/syntax.lua
G.syntax = function(C, O, U)
  return {
    Comment = { fg = C.overlay2, style = O.styles.comments }, -- just comments
    SpecialComment = { link = 'Special' }, -- special things inside a comment
    Constant = { fg = C.peach }, -- (preferred) any constant
    String = { fg = C.green, style = O.styles.strings or {} }, -- a string constant: "this is a string"
    Character = { fg = C.teal }, --  a character constant: 'c', '\n'
    Number = { fg = C.peach, style = O.styles.numbers or {} }, --   a number constant: 234, 0xff
    Float = { link = 'Number' }, --    a floating point constant: 2.3e10
    Boolean = { fg = C.peach, style = O.styles.booleans or {} }, --  a boolean constant: TRUE, false
    Identifier = { fg = C.flamingo, style = O.styles.variables or {} }, -- (preferred) any variable name
    Function = { fg = C.blue, style = O.styles.functions or {} }, -- function name (also: methods for classes)
    Statement = { fg = C.mauve }, -- (preferred) any statement
    Conditional = { fg = C.mauve, style = O.styles.conditionals or {} }, --  if, then, else, endif, switch, etc.
    Repeat = { fg = C.mauve, style = O.styles.loops or {} }, --   for, do, while, etc.
    Label = { fg = C.sapphire }, --    case, default, etc.
    Operator = { fg = C.sky, style = O.styles.operators or {} }, -- "sizeof", "+", "*", etc.
    Keyword = { fg = C.mauve, style = O.styles.keywords or {} }, --  any other keyword
    Exception = { fg = C.mauve, style = O.styles.keywords or {} }, --  try, catch, throw

    PreProc = { fg = C.pink }, -- (preferred) generic Preprocessor
    Include = { fg = C.mauve, style = O.styles.keywords or {} }, --  preprocessor #include
    Define = { link = 'PreProc' }, -- preprocessor #define
    Macro = { fg = C.mauve }, -- same as Define
    PreCondit = { link = 'PreProc' }, -- preprocessor #if, #else, #endif, etc.

    StorageClass = { fg = C.yellow }, -- static, register, volatile, etc.
    Structure = { fg = C.yellow }, --  struct, union, enum, etc.
    Special = { fg = C.pink }, -- (preferred) any special symbol
    Type = { fg = C.yellow, style = O.styles.types or {} }, -- (preferred) int, long, char, etc.
    Typedef = { link = 'Type' }, --  A typedef
    SpecialChar = { link = 'Special' }, -- special character in a constant
    Tag = { fg = C.lavender, style = { 'bold' } }, -- you can use CTRL-] on this
    Delimiter = { fg = C.overlay2 }, -- character that needs attention
    Debug = { link = 'Special' }, -- debugging statements

    Underlined = { style = { 'underline' } }, -- (preferred) text that stands out, HTML links
    Bold = { style = { 'bold' } },
    Italic = { style = { 'italic' } },
    -- ("Ignore", below, may be invisible...)
    -- Ignore = { }, -- (preferred) left blank, hidden  |hl-Ignore|

    Error = { fg = C.red }, -- (preferred) any erroneous construct
    Todo = { bg = C.flamingo, fg = C.base, style = { 'bold' } }, -- (preferred) anything that needs extra attention; mostly the keywords TODO FIXME and XXX
    qfLineNr = { fg = C.yellow },
    qfFileName = { fg = C.blue },
    htmlH1 = { fg = C.pink, style = { 'bold' } },
    htmlH2 = { fg = C.blue, style = { 'bold' } },
    -- mkdHeading = { fg = C.peach, style = { "bold" } },
    -- mkdCode = { bg = C.terminal_black, fg = C.text },
    mkdCodeDelimiter = { bg = C.base, fg = C.text },
    mkdCodeStart = { fg = C.flamingo, style = { 'bold' } },
    mkdCodeEnd = { fg = C.flamingo, style = { 'bold' } },
    -- mkdLink = { fg = C.blue, style = { "underline" } },

    -- debugging
    debugPC = { bg = O.transparent_background and C.none or C.crust }, -- used for highlighting the current line in terminal-debug
    debugBreakpoint = { bg = C.base, fg = C.overlay0 }, -- used for breakpoint colors in terminal-debug
    -- illuminate
    illuminatedWord = { bg = C.surface1 },
    illuminatedCurWord = { bg = C.surface1 },
    -- diff
    Added = { fg = C.green },
    Changed = { fg = C.blue },
    diffAdded = { fg = C.green },
    diffRemoved = { fg = C.red },
    diffChanged = { fg = C.blue },
    diffOldFile = { fg = C.yellow },
    diffNewFile = { fg = C.peach },
    diffFile = { fg = C.blue },
    diffLine = { fg = C.overlay0 },
    diffIndexLine = { fg = C.teal },
    DiffAdd = { bg = U.darken(C.green, 0.18, C.base) }, -- diff mode: Added line |diff.txt|
    DiffChange = { bg = U.darken(C.blue, 0.07, C.base) }, -- diff mode: Changed line |diff.txt|
    DiffDelete = { bg = U.darken(C.red, 0.18, C.base) }, -- diff mode: Deleted line |diff.txt|
    DiffText = { bg = U.darken(C.blue, 0.30, C.base) }, -- diff mode: Changed text within a changed line |diff.txt|
    -- NeoVim
    healthError = { fg = C.red },
    healthSuccess = { fg = C.teal },
    healthWarning = { fg = C.yellow },
    -- misc

    -- glyphs
    GlyphPalette1 = { fg = C.red },
    GlyphPalette2 = { fg = C.teal },
    GlyphPalette3 = { fg = C.yellow },
    GlyphPalette4 = { fg = C.blue },
    GlyphPalette6 = { fg = C.teal },
    GlyphPalette7 = { fg = C.text },
    GlyphPalette9 = { fg = C.red },

    -- rainbow
    rainbow1 = { fg = C.red },
    rainbow2 = { fg = C.peach },
    rainbow3 = { fg = C.yellow },
    rainbow4 = { fg = C.green },
    rainbow5 = { fg = C.sapphire },
    rainbow6 = { fg = C.lavender },

    -- csv
    csvCol0 = { fg = C.red },
    csvCol1 = { fg = C.peach },
    csvCol2 = { fg = C.yellow },
    csvCol3 = { fg = C.green },
    csvCol4 = { fg = C.sky },
    csvCol5 = { fg = C.blue },
    csvCol6 = { fg = C.lavender },
    csvCol7 = { fg = C.mauve },
    csvCol8 = { fg = C.pink },
    escCsvCol0 = { link = 'csvCol0' },

    -- markdown
    markdownHeadingDelimiter = { fg = C.peach, style = { 'bold' } },
    markdownCode = { fg = C.flamingo },
    markdownCodeBlock = { fg = C.flamingo },
    markdownLinkText = { fg = C.blue, style = { 'underline' } },
    markdownH1 = { link = 'rainbow1' },
    markdownH2 = { link = 'rainbow2' },
    markdownH3 = { link = 'rainbow3' },
    markdownH4 = { link = 'rainbow4' },
    markdownH5 = { link = 'rainbow5' },
    markdownH6 = { link = 'rainbow6' },
  }
end

-- upstream: lua/catppuccin/groups/semantic_tokens.lua
G.semantic_tokens = function(C, O, U)
  return {
    -- https://neovim.io/doc/user/lsp.html#lsp-semantic-highlight
    -- Most semantic tokens (@lsp.type.<...>) are no longer defined in this file, as Neovim links to already themed Tree-sitter captures by default.

    -- lsp's are able to detect enum members where treesitter often cannot
    ['@lsp.type.enumMember'] = { fg = C.teal },
    -- we assume treesitter can already handle this
    -- - treesitter can detect variables in buffers
    -- - lsp does not need responsibility for this, in fact it can be less
    --   accurate in cases
    ['@lsp.type.variable'] = {},

    -- in cases where the lsp can be more specific than treesitter, we should
    -- allow lsp to override treesitter
    ['@lsp.typemod.function.defaultLibrary'] = { link = '@function.builtin' },
    ['@lsp.typemod.function.builtin'] = { link = '@function.builtin' },
  }
end

-- upstream: lua/catppuccin/groups/treesitter.lua
G.treesitter = function(C, O, U)
  local colors =
    { -- Reference: https://github.com/nvim-treesitter/nvim-treesitter/blob/master/CONTRIBUTING.md
      -- Identifiers
      ['@variable'] = { fg = C.text, style = O.styles.variables or {} }, -- Any variable name that does not have another highlight.
      ['@variable.builtin'] = { fg = C.red, style = O.styles.properties or {} }, -- Variable names that are defined by the languages, like this or self.
      ['@variable.parameter'] = { fg = C.maroon, style = O.styles.variables or {} }, -- For parameters of a function.
      ['@variable.member'] = { fg = C.lavender }, -- For fields.

      ['@constant'] = { link = 'Constant' }, -- For constants
      ['@constant.builtin'] = { fg = C.peach, style = O.styles.keywords or {} }, -- For constant that are built in the language: nil in Lua.
      ['@constant.macro'] = { link = 'Macro' }, -- For constants that are defined by macros: NULL in C.

      ['@module'] = { fg = C.yellow, style = O.styles.miscs or { 'italic' } }, -- For identifiers referring to modules and namespaces.
      ['@label'] = { link = 'Label' }, -- For labels: label: in C and :label: in Lua.

      -- Literals
      ['@string'] = { link = 'String' }, -- For strings.
      ['@string.documentation'] = { fg = C.teal, style = O.styles.strings or {} }, -- For strings documenting code (e.g. Python docstrings).
      ['@string.regexp'] = { fg = C.pink, style = O.styles.strings or {} }, -- For regexes.
      ['@string.escape'] = { fg = C.pink, style = O.styles.strings or {} }, -- For escape characters within a string.
      ['@string.special'] = { link = 'Special' }, -- other special strings (e.g. dates)
      ['@string.special.path'] = { link = 'Special' }, -- filenames
      ['@string.special.symbol'] = { fg = C.flamingo }, -- symbols or atoms
      ['@string.special.url'] = { fg = C.blue, style = { 'italic', 'underline' } }, -- urls, links and emails
      ['@punctuation.delimiter.regex'] = { link = '@string.regexp' },

      ['@character'] = { link = 'Character' }, -- character literals
      ['@character.special'] = { link = 'SpecialChar' }, -- special characters (e.g. wildcards)

      ['@boolean'] = { link = 'Boolean' }, -- For booleans.
      ['@number'] = { link = 'Number' }, -- For all numbers
      ['@number.float'] = { link = 'Float' }, -- For floats.

      -- Types
      ['@type'] = { link = 'Type' }, -- For types.
      ['@type.builtin'] = { fg = C.mauve, style = O.styles.types or {} }, -- For builtin types.
      ['@type.definition'] = { link = 'Type' }, -- type definitions (e.g. `typedef` in C)

      ['@attribute'] = { link = 'Constant' }, -- attribute annotations (e.g. Python decorators)
      ['@property'] = { fg = C.lavender, style = O.styles.properties or {} }, -- For fields, like accessing `bar` property on `foo.bar`. Overriden later for data languages and CSS.

      -- Functions
      ['@function'] = { link = 'Function' }, -- For function (calls and definitions).
      ['@function.builtin'] = { fg = C.peach, style = O.styles.functions or {} }, -- For builtin functions: table.insert in Lua.
      ['@function.call'] = { link = 'Function' }, -- function calls
      ['@function.macro'] = { fg = C.pink, style = O.styles.functions or {} }, -- For macro defined functions (calls and definitions): each macro_rules in Rust.

      ['@function.method'] = { link = 'Function' }, -- For method definitions.
      ['@function.method.call'] = { link = 'Function' }, -- For method calls.

      ['@constructor'] = { fg = C.yellow }, -- For constructor calls and definitions: = { } in Lua, and Java constructors.
      ['@operator'] = { link = 'Operator' }, -- For any operator: +, but also -> and * in C.

      -- Keywords
      ['@keyword'] = { link = 'Keyword' }, -- For keywords that don't fall in previous categories.
      ['@keyword.modifier'] = { link = 'Keyword' }, -- For keywords modifying other constructs (e.g. `const`, `static`, `public`)
      ['@keyword.type'] = { link = 'Keyword' }, -- For keywords describing composite types (e.g. `struct`, `enum`)
      ['@keyword.coroutine'] = { link = 'Keyword' }, -- For keywords related to coroutines (e.g. `go` in Go, `async/await` in Python)
      ['@keyword.function'] = { fg = C.mauve, style = O.styles.keywords or {} }, -- For keywords used to define a function.
      ['@keyword.operator'] = { fg = C.mauve, style = O.styles.keywords or {} }, -- For new keyword operator
      ['@keyword.import'] = { link = 'Include' }, -- For includes: #include in C, use or extern crate in Rust, or require in Lua.
      ['@keyword.repeat'] = { link = 'Repeat' }, -- For keywords related to loops.
      ['@keyword.return'] = { fg = C.mauve, style = O.styles.keywords or {} },
      ['@keyword.debug'] = { link = 'Exception' }, -- For keywords related to debugging
      ['@keyword.exception'] = { link = 'Exception' }, -- For exception related keywords.

      ['@keyword.conditional'] = { link = 'Conditional' }, -- For keywords related to conditionnals.
      ['@keyword.conditional.ternary'] = { link = 'Operator' }, -- For ternary operators (e.g. `?` / `:`)

      ['@keyword.directive'] = { link = 'PreProc' }, -- various preprocessor directives & shebangs
      ['@keyword.directive.define'] = { link = 'Define' }, -- preprocessor definition directives
      -- JS & derivative
      ['@keyword.export'] = { fg = C.mauve, style = O.styles.keywords },

      -- Punctuation
      ['@punctuation.delimiter'] = { link = 'Delimiter' }, -- For delimiters (e.g. `;` / `.` / `,`).
      ['@punctuation.bracket'] = { fg = C.overlay2 }, -- For brackets and parenthesis.
      ['@punctuation.special'] = { link = 'Special' }, -- For special punctuation that does not fall in the categories before (e.g. `{}` in string interpolation).

      -- Comment
      ['@comment'] = { link = 'Comment' },
      ['@comment.documentation'] = { link = 'Comment' }, -- For comments documenting code

      ['@comment.error'] = { fg = C.base, bg = C.red },
      ['@comment.warning'] = { fg = C.base, bg = C.yellow },
      ['@comment.hint'] = { fg = C.base, bg = C.blue },
      ['@comment.todo'] = { fg = C.base, bg = C.flamingo },
      ['@comment.note'] = { fg = C.base, bg = C.rosewater },

      -- Markup
      ['@markup'] = { fg = C.text }, -- For strings considerated text in a markup language.
      ['@markup.strong'] = { fg = C.red, style = { 'bold' } }, -- bold
      ['@markup.italic'] = { fg = C.red, style = { 'italic' } }, -- italic
      ['@markup.strikethrough'] = { fg = C.text, style = { 'strikethrough' } }, -- strikethrough text
      ['@markup.underline'] = { link = 'Underlined' }, -- underlined text

      ['@markup.heading'] = { fg = C.blue }, -- titles like: # Example
      ['@markup.heading.markdown'] = { style = { 'bold' } }, -- bold headings in markdown, but not in HTML or other markup

      ['@markup.math'] = { fg = C.blue }, -- math environments (e.g. `$ ... $` in LaTeX)
      ['@markup.quote'] = { fg = C.pink }, -- block quotes
      ['@markup.environment'] = { fg = C.pink }, -- text environments of markup languages
      ['@markup.environment.name'] = { fg = C.blue }, -- text indicating the type of an environment

      ['@markup.link'] = { fg = C.lavender }, -- text references, footnotes, citations, etc.
      ['@markup.link.label'] = { fg = C.lavender }, -- link, reference descriptions
      ['@markup.link.url'] = { fg = C.blue, style = { 'italic', 'underline' } }, -- urls, links and emails

      ['@markup.raw'] = { fg = C.green }, -- used for inline code in markdown and for doc in python (""")

      ['@markup.list'] = { fg = C.teal },
      ['@markup.list.checked'] = { fg = C.green }, -- todo notes
      ['@markup.list.unchecked'] = { fg = C.overlay1 }, -- todo notes

      -- Diff
      ['@diff.plus'] = { link = 'diffAdded' }, -- added text (for diff files)
      ['@diff.minus'] = { link = 'diffRemoved' }, -- deleted text (for diff files)
      ['@diff.delta'] = { link = 'diffChanged' }, -- deleted text (for diff files)

      -- Tags
      ['@tag'] = { fg = C.blue }, -- Tags like HTML tag names.
      ['@tag.builtin'] = { fg = C.blue }, -- JSX tag names.
      ['@tag.attribute'] = { fg = C.yellow, style = O.styles.miscs or { 'italic' } }, -- XML/HTML attributes (foo in foo="bar").
      ['@tag.delimiter'] = { fg = C.teal }, -- Tag delimiter like < > /

      -- Misc
      ['@error'] = { link = 'Error' },

      -- Language specific:

      -- Bash
      ['@function.builtin.bash'] = {
        fg = C.red,
        style = O.styles.miscs or { 'italic' },
      },
      ['@variable.parameter.bash'] = { fg = C.green },

      -- markdown
      ['@markup.heading.1.markdown'] = { link = 'rainbow1' },
      ['@markup.heading.2.markdown'] = { link = 'rainbow2' },
      ['@markup.heading.3.markdown'] = { link = 'rainbow3' },
      ['@markup.heading.4.markdown'] = { link = 'rainbow4' },
      ['@markup.heading.5.markdown'] = { link = 'rainbow5' },
      ['@markup.heading.6.markdown'] = { link = 'rainbow6' },

      -- html
      ['@markup.heading.html'] = { link = '@markup' },
      ['@markup.heading.1.html'] = { link = '@markup' },
      ['@markup.heading.2.html'] = { link = '@markup' },
      ['@markup.heading.3.html'] = { link = '@markup' },
      ['@markup.heading.4.html'] = { link = '@markup' },
      ['@markup.heading.5.html'] = { link = '@markup' },
      ['@markup.heading.6.html'] = { link = '@markup' },

      -- Java
      ['@constant.java'] = { fg = C.teal },

      -- CSS
      ['@property.css'] = { fg = C.blue },
      ['@property.scss'] = { fg = C.blue },
      ['@property.id.css'] = { fg = C.yellow },
      ['@property.class.css'] = { fg = C.yellow },
      ['@type.css'] = { fg = C.lavender },
      ['@type.tag.css'] = { fg = C.blue },
      ['@string.plain.css'] = { fg = C.text },
      ['@number.css'] = { fg = C.peach },
      ['@keyword.directive.css'] = { link = 'Keyword' }, -- CSS at-rules: https://developer.mozilla.org/en-US/docs/Web/CSS/At-rule.

      -- HTML
      ['@string.special.url.html'] = { fg = C.green }, -- Links in href, src attributes.
      ['@markup.link.label.html'] = { fg = C.text }, -- Text between <a></a> tags.
      ['@character.special.html'] = { fg = C.red }, -- Symbols such as &nbsp;.

      -- Lua
      ['@constructor.lua'] = { link = '@punctuation.bracket' }, -- For constructor calls and definitions: = { } in Lua.

      -- Python
      ['@constructor.python'] = { fg = C.sky }, -- __init__(), __new__().

      -- YAML
      ['@label.yaml'] = { fg = C.yellow }, -- Anchor and alias names.

      -- Ruby
      ['@string.special.symbol.ruby'] = { fg = C.flamingo },

      -- PHP
      ['@function.method.php'] = { link = 'Function' },
      ['@function.method.call.php'] = { link = 'Function' },

      -- C/CPP
      ['@keyword.import.c'] = { fg = C.yellow },
      ['@keyword.import.cpp'] = { fg = C.yellow },

      -- C#
      ['@attribute.c_sharp'] = { fg = C.yellow },

      -- gitcommit
      ['@comment.warning.gitcommit'] = { fg = C.yellow },

      -- gitignore
      ['@string.special.path.gitignore'] = { fg = C.text },

      -- Misc
      gitcommitSummary = { fg = C.rosewater, style = O.styles.miscs or { 'italic' } },
      zshKSHFunction = { link = 'Function' },
    }

  -- Legacy highlights
  colors['@parameter'] = colors['@variable.parameter']
  colors['@field'] = colors['@variable.member']
  colors['@namespace'] = colors['@module']
  colors['@float'] = colors['@number.float']
  colors['@symbol'] = colors['@string.special.symbol']
  colors['@string.regex'] = colors['@string.regexp']

  colors['@text'] = colors['@markup']
  colors['@text.strong'] = colors['@markup.strong']
  colors['@text.emphasis'] = colors['@markup.italic']
  colors['@text.underline'] = colors['@markup.underline']
  colors['@text.strike'] = colors['@markup.strikethrough']
  colors['@text.uri'] = colors['@markup.link.url']
  colors['@text.math'] = colors['@markup.math']
  colors['@text.environment'] = colors['@markup.environment']
  colors['@text.environment.name'] = colors['@markup.environment.name']

  colors['@text.title'] = colors['@markup.heading']
  colors['@text.literal'] = colors['@markup.raw']
  colors['@text.reference'] = colors['@markup.link']

  colors['@text.todo.checked'] = colors['@markup.list.checked']
  colors['@text.todo.unchecked'] = colors['@markup.list.unchecked']

  colors['@comment.note'] = colors['@comment.hint']

  -- @text.todo is now for todo comments, not todo notes like in markdown
  colors['@text.todo'] = colors['@comment.todo']
  colors['@text.warning'] = colors['@comment.warning']
  colors['@text.note'] = colors['@comment.note']
  colors['@text.danger'] = colors['@comment.error']

  -- @text.uri is now
  -- > @markup.link.url in markup links
  -- > @string.special.url outside of markup
  colors['@text.uri'] = colors['@markup.link.uri']

  colors['@method'] = colors['@function.method']
  colors['@method.call'] = colors['@function.method.call']

  colors['@text.diff.add'] = colors['@diff.plus']
  colors['@text.diff.delete'] = colors['@diff.minus']

  colors['@type.qualifier'] = colors['@keyword.modifier']
  colors['@keyword.storage'] = colors['@keyword.modifier']
  colors['@define'] = colors['@keyword.directive.define']
  colors['@preproc'] = colors['@keyword.directive']
  colors['@storageclass'] = colors['@keyword.storage']
  colors['@conditional'] = colors['@keyword.conditional']
  colors['@exception'] = colors['@keyword.exception']
  colors['@include'] = colors['@keyword.import']
  colors['@repeat'] = colors['@keyword.repeat']

  colors['@symbol.ruby'] = colors['@string.special.symbol.ruby']

  colors['@variable.member.yaml'] = colors['@field.yaml']

  colors['@text.title.1.markdown'] = colors['@markup.heading.1.markdown']
  colors['@text.title.2.markdown'] = colors['@markup.heading.2.markdown']
  colors['@text.title.3.markdown'] = colors['@markup.heading.3.markdown']
  colors['@text.title.4.markdown'] = colors['@markup.heading.4.markdown']
  colors['@text.title.5.markdown'] = colors['@markup.heading.5.markdown']
  colors['@text.title.6.markdown'] = colors['@markup.heading.6.markdown']

  colors['@method.php'] = colors['@function.method.php']
  colors['@method.call.php'] = colors['@function.method.call.php']

  return colors
end

-- upstream: lua/catppuccin/groups/integrations/neotree.lua
G.neotree = function(C, O, U)
  local active_bg = O.transparent_background and C.none or C.mantle
  local inactive_bg = O.transparent_background and C.none or C.base
  return {
    NeoTreeDirectoryName = { fg = C.blue },
    NeoTreeDirectoryIcon = { fg = C.blue },
    NeoTreeNormal = { fg = C.text, bg = active_bg },
    NeoTreeNormalNC = { fg = C.text, bg = active_bg },
    NeoTreeExpander = { fg = C.overlay0 },
    NeoTreeIndentMarker = { fg = C.overlay0 },
    NeoTreeRootName = { fg = C.blue, style = { 'bold' } },
    NeoTreeSymbolicLinkTarget = { fg = C.pink },
    NeoTreeModified = { fg = C.peach },

    NeoTreeGitAdded = { fg = C.green },
    NeoTreeGitConflict = { fg = C.red },
    NeoTreeGitDeleted = { fg = C.red },
    NeoTreeGitIgnored = { fg = C.overlay0 },
    NeoTreeGitModified = { fg = C.yellow },
    NeoTreeGitUnstaged = { fg = C.red },
    NeoTreeGitUntracked = { fg = C.mauve },
    NeoTreeGitStaged = { fg = C.green },

    NeoTreeFloatBorder = { link = 'FloatBorder' },
    NeoTreeFloatTitle = { link = 'FloatTitle' },
    NeoTreeTitleBar = { fg = C.mantle, bg = C.blue },

    NeoTreeFileNameOpened = { fg = C.pink },
    NeoTreeDimText = { fg = C.overlay1 },
    NeoTreeFilterTerm = { fg = C.green, style = { 'bold' } },
    NeoTreeTabActive = { bg = active_bg, fg = C.lavender, style = { 'bold' } },
    NeoTreeTabInactive = { bg = inactive_bg, fg = C.overlay0 },
    NeoTreeTabSeparatorActive = { fg = active_bg, bg = active_bg },
    NeoTreeTabSeparatorInactive = { fg = inactive_bg, bg = inactive_bg },
    NeoTreeVertSplit = { fg = C.base, bg = inactive_bg },
    NeoTreeWinSeparator = {
      fg = O.transparent_background and C.surface1 or C.base,
      bg = O.transparent_background and C.none or C.base,
    },
    NeoTreeStatusLineNC = { fg = C.mantle, bg = C.mantle },
  }
end

-- upstream: lua/catppuccin/groups/integrations/telescope.lua
G.telescope = function(C, O, U)
  local hlgroups = {
    TelescopeBorder = { link = 'FloatBorder' },
    TelescopeNormal = { link = 'NormalFloat' },
    TelescopePreviewNormal = { link = 'TelescopeNormal' },
    TelescopePromptNormal = { link = 'TelescopeNormal' },
    TelescopeResultsNormal = { link = 'TelescopeNormal' },
    TelescopeTitle = { link = 'FloatTitle' },
    TelescopeSelectionCaret = { fg = C.flamingo, bg = C.surface0 },
    TelescopeSelection = {
      fg = C.flamingo,
      bg = C.surface0,
      style = { 'bold' },
    },
    TelescopeMatching = { fg = C.blue },
    TelescopePromptPrefix = { fg = C.flamingo },
  }

  if O.float.solid then
    hlgroups['TelescopePreviewTitle'] = {
      fg = C.crust,
      bg = C.green,
    }
    hlgroups['TelescopePromptTitle'] = {
      fg = C.crust,
      bg = C.red,
    }
    hlgroups['TelescopeResultsTitle'] = {
      fg = C.crust,
      bg = C.lavender,
    }
  end

  return hlgroups
end

-- upstream: lua/catppuccin/groups/integrations/rainbow_delimiters.lua
G.rainbow_delimiters = function(C, O, U)
  return {
    RainbowDelimiterRed = { fg = C.red },
    RainbowDelimiterYellow = { fg = C.yellow },
    RainbowDelimiterBlue = { fg = C.blue },
    RainbowDelimiterOrange = { fg = C.peach },
    RainbowDelimiterGreen = { fg = C.green },
    RainbowDelimiterViolet = { fg = C.mauve },
    RainbowDelimiterCyan = { fg = C.teal },
  }
end

-- upstream: lua/catppuccin/utils/lualine.lua (default colours)
G.lualine = function(C, O)
  local transparent_bg = O.transparent_background and 'NONE' or C.mantle
  local default_colors = {
    normal = {
      a = { bg = C.blue, fg = C.mantle, gui = 'bold' },
      b = { bg = C.surface0, fg = C.blue },
      c = { bg = transparent_bg, fg = C.text },
    },

    insert = {
      a = { bg = C.green, fg = C.base, gui = 'bold' },
      b = { bg = C.surface0, fg = C.green },
    },

    terminal = {
      a = { bg = C.green, fg = C.base, gui = 'bold' },
      b = { bg = C.surface0, fg = C.green },
    },

    command = {
      a = { bg = C.peach, fg = C.base, gui = 'bold' },
      b = { bg = C.surface0, fg = C.peach },
    },
    visual = {
      a = { bg = C.mauve, fg = C.base, gui = 'bold' },
      b = { bg = C.surface0, fg = C.mauve },
    },
    replace = {
      a = { bg = C.red, fg = C.base, gui = 'bold' },
      b = { bg = C.surface0, fg = C.red },
    },
    inactive = {
      a = { bg = transparent_bg, fg = C.blue },
      b = { bg = transparent_bg, fg = C.surface1, gui = 'bold' },
      c = { bg = transparent_bg, fg = C.overlay0 },
    },
  }
  return default_colors
end

---------------------------------------------------------------------------
-- palette + theme assembly (catppuccin.palettes, lib.mapper, lib.compiler)
---------------------------------------------------------------------------

local function get_palette(flavour, O)
  local C = deep_keep(
    O.color_overrides.all or {},
    O.color_overrides[flavour] or {},
    M.palettes[flavour]
  )
  C.none = 'NONE'
  local p = O.dim_inactive.percentage
  C.dim = O.dim_inactive.shade == 'dark'
      and U.vary_color(
        { latte = U.darken(C.base, p, C.mantle) },
        U.darken(C.base, p, C.mantle)
      )
    or U.vary_color(
      { latte = U.lighten('#FBFCFD', p, C.base) },
      U.lighten(C.surface0, p, C.base)
    )
  return C
end

-- the nvim-named table: custom > integrations > syntax > editor, styles compiled
local function assemble(flavour, O, C)
  local editor = deep_force(G.editor(C, O, U), G.lsp(C, O, U))
  local syntax = {}
  for _, g in ipairs({ 'syntax', 'semantic_tokens', 'treesitter' }) do
    syntax = deep_force(syntax, G[g](C, O, U))
  end
  local integrations = {}
  for _, g in ipairs({ 'neotree', 'telescope', 'rainbow_delimiters' }) do
    integrations = deep_force(integrations, G[g](C, O, U))
  end

  local hl = O.highlight_overrides
  local per = hl[flavour]
  if type(per) == 'function' then per = per(C) end
  local all = hl.all
  if type(all) == 'function' then all = all(C) end
  local custom = deep_keep(per or {}, all or {})

  local tbl = deep_keep(custom, integrations, syntax, editor)
  for group, color in pairs(tbl) do
    if color.style then
      for _, style in pairs(color.style) do
        color[style] = true
        if O.no_italic and style == 'italic' then color[style] = false end
        if O.no_bold and style == 'bold' then color[style] = false end
        if O.no_underline and style == 'underline' then color[style] = false end
      end
    end
    color.style = nil
    if color.link and custom[group] and not custom[group].link then
      color.link = nil
    end
  end
  return tbl, custom
end

---------------------------------------------------------------------------
-- projection onto winter groups
---------------------------------------------------------------------------

local FLAGS =
  { 'bold', 'italic', 'underline', 'undercurl', 'strikethrough', 'reverse' }

local function solid(c)
  if c == nil or c == 'NONE' then return nil end
  return c
end

-- nvim highlight def -> winter.Style
local function to_style(d)
  if not d then return nil end
  local s = { fg = solid(d.fg), bg = solid(d.bg), ul = solid(d.sp) }
  for _, f in ipairs(FLAGS) do
    if d[f] then s[f] = true end
  end
  return s
end

-- the attribute flags of a def, without its colours
local function flags_of(d)
  local o = {}
  for _, f in ipairs(FLAGS) do
    if d and d[f] then o[f] = true end
  end
  return o
end

local function pick(d, ...)
  local o = {}
  for _, k in ipairs({ ... }) do
    if d then o[k] = d[k] end
  end
  return o
end

-- lualine section { fg, bg, gui = "bold" } -> def
local function section(s)
  local d = { fg = s.fg, bg = s.bg }
  for f in tostring(s.gui or ''):gmatch('%a+') do
    d[f] = true
  end
  return d
end

-- @capture / @lsp.* / native winter name -> winter group, or nil
local function group_name(name)
  local cap = name:match('^@(.+)$')
  if cap then
    local t = cap:match('^lsp%.type%.(.+)$')
    if t then return 'lsp.' .. t end
    t = cap:match('^lsp%.typemod%.(.+)$')
    if t then return 'lsp.' .. t end
    if cap:match('^lsp%.') then return nil end -- @lsp.mod.*: no winter equivalent
    return cap
  end
  if name:match('^ui%.') or name:match('^diag%.') or name:match('^lsp%.') then
    return name
  end
  return nil
end

local function native(name)
  return name:match('^ui%.') or name:match('^diag%.') or name:match('^lsp%.')
end

-- winter group <- nvim group(s). Values are nvim-style defs.
local function ui_groups(R, C, O)
  local L = G.lualine(C, O)
  local cursor_fg = O.cursor and (C[O.cursor] or O.cursor) or R('Cursor').bg
  local vt, ul = O.lsp_styles.virtual_text, O.lsp_styles.underlines
  return {
    -- editor
    ['ui.normal'] = R('Normal'),
    ['ui.message'] = R('Normal'),
    ['ui.linenr'] = R('LineNr'),
    ['ui.linenr.cursor'] = R('CursorLineNr'),
    ['ui.tilde'] = R('EndOfBuffer'),
    ['ui.ctrl'] = R('NonText'), -- SpecialKey links NonText
    ['ui.indent_lead'] = R('NonText'),
    ['ui.wrap'] = R('NonText'),
    ['ui.visual'] = R('Visual'),
    ['ui.search'] = R('Search'),
    ['ui.search.current'] = R('CurSearch'),
    ['ui.cursor'] = { fg = cursor_fg },
    ['ui.search.cursor'] = { fg = cursor_fg },
    ['ui.cursorline'] = R('CursorLine'),
    ['ui.colorcolumn'] = R('ColorColumn'),
    ['ui.matchparen'] = R('MatchParen'),
    ['ui.subst.match'] = R('IncSearch'),
    ['ui.subst.replace'] = R('Substitute'),
    ['ui.reference'] = R('LspReferenceText'),
    ['ui.reference.read'] = R('LspReferenceRead'),
    ['ui.reference.write'] = R('LspReferenceWrite'),
    -- floats, input, hover
    ['ui.float'] = R('NormalFloat'),
    ['ui.float.border'] = R('FloatBorder'),
    ['ui.float.title'] = R('FloatTitle'),
    ['ui.hover'] = R('NormalFloat'),
    ['ui.input'] = R('NormalFloat'),
    ['ui.input.border'] = R('FloatBorder'),
    ['ui.input.prompt'] = R('Title'),
    -- picker (telescope)
    ['ui.picker.border'] = R('TelescopeBorder'),
    ['ui.picker.prompt'] = R('TelescopePromptPrefix'),
    -- menus (pmenu + telescope)
    ['ui.menu'] = R('Pmenu'),
    ['ui.menu.select'] = R('PmenuSel'),
    ['ui.menu.kind'] = R('PmenuKind'),
    ['ui.menu.detail'] = R('PmenuExtra'),
    ['ui.menu.dir'] = R('PmenuExtra'),
    ['ui.menu.match'] = R('TelescopeMatching'),
    ['ui.menu.marker'] = pick(R('TelescopeSelectionCaret'), 'fg'),
    ['ui.menu.doc'] = pick(R('NormalFloat'), 'fg'),
    -- diagnostics. winter has one virtual-text/underline group for all
    -- severities: errors' style is used, colours follow the severity.
    ['diag.error'] = pick(R('DiagnosticError'), 'fg'),
    ['diag.warn'] = pick(R('DiagnosticWarn'), 'fg'),
    ['diag.info'] = pick(R('DiagnosticInfo'), 'fg'),
    ['diag.hint'] = pick(R('DiagnosticHint'), 'fg'),
    ['diag.virtual'] = flags_of(R('DiagnosticVirtualTextError')),
    ['diag.underline'] = flags_of(R('DiagnosticUnderlineError')),
    -- file tree (neo-tree)
    ['ui.tree.file'] = pick(R('NeoTreeNormal'), 'fg'),
    ['ui.tree.dir'] = R('NeoTreeDirectoryName'),
    ['ui.tree.root'] = R('NeoTreeRootName'),
    ['ui.tree.open'] = R('NeoTreeFileNameOpened'),
    ['ui.tree.active'] = R('NeoTreeFileNameOpened'),
    ['ui.tree.symlink'] = R('NeoTreeSymbolicLinkTarget'),
    ['ui.tree.hidden'] = R('NeoTreeGitIgnored'),
    ['ui.tree.guide'] = R('NeoTreeIndentMarker'),
    ['ui.tree.sep'] = pick(R('WinSeparator'), 'fg'),
    ['ui.tree.select'] = pick(R('CursorLine'), 'bg'),
    ['ui.tree.match'] = R('NeoTreeFilterTerm'),
    ['ui.tree.icon.dir'] = R('NeoTreeDirectoryIcon'),
    ['ui.tree.git.modified'] = R('NeoTreeGitModified'),
    ['ui.tree.git.added'] = R('NeoTreeGitAdded'),
    ['ui.tree.git.deleted'] = R('NeoTreeGitDeleted'),
    ['ui.tree.git.untracked'] = R('NeoTreeGitUntracked'),
    ['ui.tree.git.conflict'] = R('NeoTreeGitConflict'),
    ['ui.tree.git.staged'] = R('NeoTreeGitStaged'),
    -- statusline (the lualine theme)
    ['ui.statusline'] = section(L.normal.c),
    ['ui.statusline.normal'] = section(L.normal.a),
    ['ui.statusline.insert'] = section(L.insert.a),
    ['ui.statusline.visual'] = section(L.visual.a),
    ['ui.statusline.vline'] = section(L.visual.a),
    ['ui.statusline.replace'] = section(L.replace.a),
    ['ui.statusline.command'] = section(L.command.a),
    -- rainbow_delimiters, in winter's palette order
    _rainbow = {
      R('RainbowDelimiterRed').fg,
      R('RainbowDelimiterYellow').fg,
      R('RainbowDelimiterBlue').fg,
      R('RainbowDelimiterOrange').fg,
      R('RainbowDelimiterGreen').fg,
      R('RainbowDelimiterViolet').fg,
      R('RainbowDelimiterCyan').fg,
    },
  }
end

-- winter has things catppuccin/nvim has no group for, and engine defaults that
-- are hard-coded mocha. These are palette-driven so every flavour is covered.
local function winter_only(C, U)
  return {
    ['ui.welcome'] = { fg = C.overlay0 },
    ['ui.trailing'] = { bg = U.darken(C.red, 0.30, C.base) },
    ['ui.snippet'] = { bg = C.surface0 },
    ['ui.snippet.active'] = { bg = C.surface1 },
    ['ui.menu.key'] = { fg = C.lavender, bold = true },
    ['ui.menu.disabled'] = { fg = C.overlay0 },
    -- modes lualine has no pill for (engine defaults: yellow / teal)
    ['ui.statusline.search'] = { fg = C.base, bg = C.yellow, bold = true },
    ['ui.statusline.files'] = { fg = C.base, bg = C.lavender, bold = true },
    -- chips: neutral stand-ins for lualine's mode-coloured b section
    ['ui.statusline.file'] = { fg = C.text, bg = C.surface0 },
    ['ui.statusline.position'] = { fg = C.text, bg = C.surface0 },
    ['ui.statusline.modified'] = { fg = C.peach },
  }
end

-- Completion menu icons: one colour per LSP CompletionItemKind (glyphs live in
-- kKinds in complete.cpp). The per-kind groups outrank the blanket
-- ui.menu.kind (PmenuKind, which is plain blue), so without these every icon
-- comes out blue.
local function completion_kinds(C)
  local by_kind = {
    -- callables
    method = C.blue,
    ['function'] = C.blue,
    constructor = C.sapphire,
    -- data
    variable = C.lavender,
    field = C.flamingo,
    property = C.teal,
    -- types
    class = C.yellow,
    struct = C.yellow,
    interface = C.yellow,
    enum = C.yellow,
    type_parameter = C.maroon,
    -- constants and values
    constant = C.peach,
    enum_member = C.peach,
    value = C.peach,
    unit = C.peach,
    -- everything else
    module = C.rosewater,
    keyword = C.mauve,
    snippet = C.green,
    color = C.pink,
    operator = C.sky,
    event = C.red,
    file = C.subtext0,
    folder = C.blue,
    reference = C.subtext0,
    text = C.subtext0,
    unknown = C.overlay1,
  }
  local out = {}
  for kind, fg in pairs(by_kind) do
    out['ui.menu.kind.' .. kind] = { fg = fg }
  end
  return out
end

local function merge_style(base, over)
  local o = {}
  for k, v in pairs(base or {}) do
    o[k] = v
  end
  for k, v in pairs(over or {}) do
    o[k] = v
  end
  return o
end

-- nvim's built-in @lsp.type.* links (:h lsp-semantic-highlight). Upstream only
-- overrides a few, so the rest come from these. Limited to the token types
-- winter's engine styles at all; winter maps them to captures itself and some
-- of those differ (macro -> "define" there, but @constant.macro in nvim).
local NVIM_LSP_DEFAULTS = {
  class = '@type',
  enum = '@type',
  interface = '@type',
  struct = '@type',
  type = '@type',
  typeParameter = '@type.definition',
  decorator = '@attribute',
  ['function'] = '@function',
  macro = '@constant.macro',
  method = '@function.method',
  namespace = '@module',
  parameter = '@variable.parameter',
  property = '@property',
}

---@return table groups winter.theme.apply() entries
---@return string[] rainbow
---@return table C palette (with none/dim)
---@return string[] missing nvim groups the projection asked for and upstream does not define
local function build(flavour, O)
  M.flavour = flavour -- U.vary_color reads it
  local C = get_palette(flavour, O)
  local tbl, custom = assemble(flavour, O, C)

  local missing = {}
  local function resolve(name)
    local d, hops = tbl[name], 0
    while d and d.link do
      hops = hops + 1
      if hops > 32 then return nil end
      d = tbl[d.link]
    end
    return d
  end
  local function R(name)
    local d = resolve(name)
    if d == nil then missing[#missing + 1] = name end
    return d or {}
  end

  local out = {}

  -- 1. captures and semantic tokens, links resolved
  for name in pairs(tbl) do
    local g = group_name(name)
    if g and not native(name) then
      local st = to_style(resolve(name))
      if st then
        if g:match('^lsp%.') and next(st) == nil then
          out[g] = false
        else
          out[g] = st
        end
      end
    end
  end

  -- 1b. semantic token types nvim links by default and upstream leaves alone.
  --     `lsp.variable` stays unset on purpose (upstream: @lsp.type.variable =
  --     {}): setting it would also restyle parameter tokens in winter.
  for t, target in pairs(NVIM_LSP_DEFAULTS) do
    local g = 'lsp.' .. t
    if out[g] == nil then
      local c = target
      local d = resolve(c)
      while d == nil do -- nvim falls back to the parent capture
        c = c:match('^(.+)%.[^.]+$')
        if not c then break end
        d = resolve(c)
      end
      local st = to_style(d)
      if st then out[g] = st end
    end
  end

  -- 2. editor / plugin groups
  local ui = ui_groups(R, C, O)
  local rainbow = ui._rainbow
  ui._rainbow = nil
  for g, d in pairs(ui) do
    out[g] = to_style(d)
  end
  for g, st in pairs(winter_only(C, U)) do
    out[g] = st
  end

  for g, st in pairs(winter_only(C, U)) do
    out[g] = st
  end
  for g, st in pairs(completion_kinds(C)) do -- <- new
    out[g] = st
  end

  -- 3. the engine's built-in capture colours are mocha constants: anything
  --    upstream leaves unstyled must be stated, or they leak through.
  for _, k in ipairs(ENGINE_CAPTURES) do
    local c, covered = k, false
    while c do
      if out[c] ~= nil then
        covered = true
        break
      end
      c = c:match('^(.+)%.[^.]+$')
    end
    if not covered then out[k] = {} end
  end

  -- 4. user overrides addressed to winter groups ("ui.*", "diag.*", "lsp.*")
  for name in pairs(custom) do
    if native(name) then
      local st = to_style(resolve(name))
      if st then
        out[name] = merge_style(type(out[name]) == 'table' and out[name] or nil, st)
      end
    end
  end

  return out, rainbow, C, missing
end

function M.groups(flavour, opts)
  local O = deep_keep(opts or {}, M.default_options)
  if flavour == nil or flavour == 'auto' then
    flavour = O.flavour == 'auto' and O.background.dark or O.flavour
  end
  assert(M.palettes[flavour], 'catppuccin: unknown flavour ' .. tostring(flavour))
  return build(flavour, O)
end

---------------------------------------------------------------------------
-- setup
---------------------------------------------------------------------------

function M.setup(opts)
  M._user = opts or {}
  local O = deep_keep(M._user, M.default_options)
  local flavour = O.flavour
  if flavour == 'auto' then flavour = O.background.dark end
  assert(M.palettes[flavour], 'catppuccin: unknown flavour ' .. tostring(flavour))
  M.options = O

  local out, rainbow, C = build(flavour, O)
  M.flavour, M.colors = flavour, C

  winter.theme.reset() -- like `hi clear` on colorscheme load
  winter.theme.apply(out)
  if O.rainbow ~= false then winter.theme.rainbow(rainbow) end

  local extra = O.overrides
  if type(extra) == 'function' then extra = extra(C, U) end
  if type(extra) == 'table' then winter.theme.apply(extra) end

  if not M._cmd then
    M._cmd = true
    winter.cmd.add('Catppuccin', function(args)
      local f = (args.args or ''):match('^%s*(%S+)')
      if f and M.palettes[f] then
        M.load(f)
      else
        winter.ui.status('catppuccin: flavours are latte, frappe, macchiato, mocha')
      end
    end, {
      desc = 'Catppuccin <latte|frappe|macchiato|mocha>',
      nargs = '1',
      complete = function(lead)
        local r = {}
        for _, f in ipairs({ 'latte', 'frappe', 'macchiato', 'mocha' }) do
          if f:sub(1, #lead) == lead then r[#r + 1] = f end
        end
        return r
      end,
    })
  end
  return C
end

--- Switch flavour, keeping the options given to setup().
function M.load(flavour)
  local o = deep_keep({ flavour = flavour }, M._user or {})
  return M.setup(o)
end

M.options = deep_keep({}, M.default_options)
M.flavour = 'mocha'

---------------------------------------------------------------------------
-- Where winter cannot show upstream's look (engine limits, not port gaps)
--   * diag.virtual is one group for every severity, so upstream's per-severity
--     tinted background (darken(severity, 0.095, base)) is not applied; the
--     italic style and the severity foreground are.
--   * lualine's b section (text coloured like the mode) cannot follow the mode
--     here: ui.statusline.file/position are text on surface0. The right-hand
--     percent pill follows the mode through winter's built-in (mocha) accents.
--   * neo-tree's mantle panel background (NeoTreeNormal.bg): ui.tree.file only
--     styles the names.
--   * diag.underline is one group too: errors' style is used. Upstream's
--     default is a plain underline; for curls use
--     lsp_styles = { underlines = { errors = { "undercurl" } } }.
--   * winter maps rust-analyzer's extra token types (typeAlias, lifetime,
--     builtinType, ...) to captures; nvim styles none of them, so there the
--     tree-sitter colour always shows. Same palette, same captures, but the
--     LSP layer now sits on top.
--   * Nothing to style in winter: dim_inactive, term_colors, inlay hints, signs,
--     tabline, plugins. @lsp.mod.* tokens have no winter lookup.
---------------------------------------------------------------------------

return M
