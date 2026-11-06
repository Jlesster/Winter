-- gruvbox (dark) for winter
--
-- add to init.lua:
--     require("gruvbox")
--
-- Layers, from most to least general:
--   1. tree-sitter captures   ("keyword", "function", "operator", ...)
--   2. lsp semantic tokens    ("lsp.<tokenType>", "lsp.mod.<modifier>")
--   3. ui chrome              ("ui.*", "diag.*")
--
-- Notes on how winter resolves things, since this theme leans on them:
--   * Captures are layered by (priority, capture order) like neovim, so the
--     `&` / `*` / `&&` inside a parameter declaration is an "operator", while
--     the parameter name stays "variable.parameter".
--   * A semantic token paints over tree-sitter. Types with no `lsp.<type>`
--     style (and no built-in default) are left to tree-sitter. A modifier rule
--     that sets no fg only adds attributes, so it never recolours a token.

local c = {
  bg0_h = '#1d2021',
  bg0 = '#282828',
  bg1 = '#3c3836',
  bg2 = '#504945',
  bg3 = '#665c54',
  bg4 = '#7c6f64',
  fg0 = '#fbf1c7',
  fg1 = '#ebdbb2',
  fg2 = '#d5c4a1',
  fg3 = '#bdae93',
  fg4 = '#a89984',
  gray = '#928374',
  red = '#fb4934',
  green = '#b8bb26',
  yellow = '#fabd2f',
  blue = '#83a598',
  purple = '#d3869b',
  aqua = '#8ec07c',
  orange = '#fe8019',
  red_d = '#cc241d',
  green_d = '#98971a',
  yellow_d = '#d79921',
  blue_d = '#458588',
  purple_d = '#b16286',
  aqua_d = '#689d6a',
  orange_d = '#d65d0e',
}

-- ---------------------------------------------------------------------------
-- 1. tree-sitter captures
-- ---------------------------------------------------------------------------
local syntax = {
  keyword = { fg = c.red },
  conditional = { fg = c.red },
  ['repeat'] = { fg = c.red },
  exception = { fg = c.red },
  storageclass = { fg = c.orange },
  include = { fg = c.aqua },
  preproc = { fg = c.aqua },
  define = { fg = c.aqua },
  ['keyword.directive'] = { fg = c.aqua },
  ['function.macro'] = { fg = c.aqua },

  string = { fg = c.green },
  character = { fg = c.green },
  ['string.escape'] = { fg = c.orange },
  escape = { fg = c.orange },
  ['string.regexp'] = { fg = c.orange },
  ['string.special'] = { fg = c.aqua },
  ['string.special.url'] = { fg = c.blue, underline = true },
  ['character.special'] = { fg = c.orange },

  number = { fg = c.purple },
  float = { fg = c.purple },
  boolean = { fg = c.purple },
  constant = { fg = c.purple },

  ['function'] = { fg = c.green, bold = true },
  method = { fg = c.green, bold = true },
  ['function.builtin'] = { fg = c.orange, bold = true },
  constructor = { fg = c.yellow },

  type = { fg = c.yellow },
  attribute = { fg = c.aqua },
  namespace = { fg = c.aqua },
  module = { fg = c.aqua },
  label = { fg = c.aqua },

  property = { fg = c.blue },
  field = { fg = c.blue },
  ['variable.member'] = { fg = c.blue },
  ['variable.parameter'] = { fg = c.purple, italic = true },
  parameter = { fg = c.purple, italic = true },
  ['variable.builtin'] = { fg = c.orange },

  -- operators (including `&`, `*`, `&&` in parameter lists) share one colour
  operator = { fg = c.aqua },
  punctuation = { fg = c.fg4 },

  comment = { fg = c.gray, italic = true },
  ['comment.error'] = { fg = c.red, bold = true },
  ['comment.warning'] = { fg = c.yellow, bold = true },
  ['comment.todo'] = { fg = c.blue, bold = true },
  ['comment.note'] = { fg = c.blue, bold = true },

  tag = { fg = c.aqua },
  ['tag.attribute'] = { fg = c.yellow },
  ['tag.delimiter'] = { fg = c.fg4 },

  ['markup.heading'] = { fg = c.green, bold = true },
  ['markup.strong'] = { bold = true },
  ['markup.italic'] = { italic = true },
  ['markup.strikethrough'] = { strikethrough = true },
  ['markup.link'] = { fg = c.purple, underline = true },
  ['markup.link.url'] = { fg = c.blue, underline = true },
  ['markup.raw'] = { fg = c.aqua },
  ['markup.list'] = { fg = c.red },
  ['markup.quote'] = { fg = c.gray, italic = true },

  ['diff.plus'] = { fg = c.green },
  ['diff.minus'] = { fg = c.red },
  ['diff.delta'] = { fg = c.yellow },
}

-- ---------------------------------------------------------------------------
-- 2. lsp semantic tokens
-- ---------------------------------------------------------------------------
local lsp = {
  -- declarations of things
  ['lsp.namespace'] = { fg = c.aqua },
  ['lsp.type'] = { fg = c.yellow, bold = true },
  ['lsp.class'] = { fg = c.yellow, bold = true },
  ['lsp.struct'] = { fg = c.yellow, bold = true },
  ['lsp.enum'] = { fg = c.yellow, bold = true },
  ['lsp.interface'] = { fg = c.yellow, bold = true },
  ['lsp.concept'] = { fg = c.aqua, bold = true },
  ['lsp.typeParameter'] = { fg = c.yellow, italic = true },

  -- values: members, locals and parameters are italic, each its own colour
  ['lsp.variable'] = { fg = c.fg1, italic = true },
  ['lsp.parameter'] = { fg = c.purple, italic = true },
  ['lsp.property'] = { fg = c.blue, italic = true },
  ['lsp.enumMember'] = { fg = c.aqua, italic = true },

  ['lsp.function'] = { fg = c.green, bold = true },
  ['lsp.method'] = { fg = c.green, bold = true },
  ['lsp.macro'] = { fg = c.aqua },
  ['lsp.decorator'] = { fg = c.aqua },
  ['lsp.label'] = { fg = c.aqua },

  -- extra vocabulary from rust-analyzer / kotlin / lua servers
  ['lsp.typeAlias'] = { fg = c.yellow, bold = true },
  ['lsp.union'] = { fg = c.yellow, bold = true },
  ['lsp.trait'] = { fg = c.yellow, bold = true, italic = true },
  ['lsp.generic'] = { fg = c.yellow, italic = true },
  ['lsp.builtinType'] = { fg = c.yellow, bold = true },
  ['lsp.selfKeyword'] = { fg = c.red, italic = true },
  ['lsp.selfTypeKeyword'] = { fg = c.yellow, italic = true },
  ['lsp.lifetime'] = { fg = c.aqua, italic = true },
  ['lsp.derive'] = { fg = c.aqua },
  ['lsp.deriveHelper'] = { fg = c.aqua },
  ['lsp.builtinAttribute'] = { fg = c.aqua },
  ['lsp.attribute'] = { fg = c.aqua },
  ['lsp.escapeSequence'] = { fg = c.orange },
  ['lsp.formatSpecifier'] = { fg = c.orange },
  ['lsp.boolean'] = { fg = c.purple },

  -- modifiers add attributes only (no fg), so they never recolour a token.
  -- `deprecated` is struck through by default; nothing to set for it.
  ['lsp.mod.usedAsMutableReference'] = { underline = true },
  ['lsp.mod.usedAsMutablePointer'] = { underline = true },
}

-- ---------------------------------------------------------------------------
-- 3. ui chrome
-- ---------------------------------------------------------------------------
local mode = function(bg) return { fg = c.bg0_h, bg = bg, bold = true } end

local ui = {
  ['ui.normal'] = { fg = c.fg1, bg = c.bg0 },
  ['ui.message'] = { fg = c.fg3 },
  ['ui.welcome'] = { fg = c.gray },
  ['ui.ctrl'] = { fg = c.bg4 },
  ['ui.tilde'] = { fg = c.bg2 },
  ['ui.linenr'] = { fg = c.bg4 },
  ['ui.linenr.cursor'] = { fg = c.yellow, bold = true },
  ['ui.cursor'] = { fg = c.fg1 },
  ['ui.cursorline'] = { bg = c.bg1 },
  ['ui.colorcolumn'] = { bg = c.bg1 },
  ['ui.matchparen'] = { bg = c.bg3, bold = true },
  ['ui.trailing'] = { bg = c.red_d },
  ['ui.indent_lead'] = { fg = c.gray },
  ['ui.wrap'] = { fg = c.gray },

  ['ui.visual'] = { bg = c.bg3 },
  ['ui.search'] = { fg = c.bg0_h, bg = c.yellow_d },
  ['ui.search.current'] = { fg = c.bg0_h, bg = c.orange, bold = true },
  ['ui.search.cursor'] = { fg = c.bg0_h, bg = c.red, bold = true },
  ['ui.subst.match'] = { fg = c.bg0_h, bg = c.red },
  ['ui.subst.replace'] = { fg = c.bg0_h, bg = c.green },

  -- lsp document highlight + snippets
  ['ui.reference'] = { bg = c.bg2 },
  ['ui.reference.read'] = { bg = c.bg2 },
  ['ui.reference.write'] = { bg = c.bg3, underline = true },
  ['ui.snippet'] = { bg = c.bg2 },
  ['ui.snippet.active'] = { bg = c.bg3, underline = true },

  -- status line
  ['ui.statusline'] = { fg = c.fg3, bg = c.bg1 },
  ['ui.statusline.normal'] = mode(c.fg4),
  ['ui.statusline.insert'] = mode(c.blue),
  ['ui.statusline.visual'] = mode(c.orange),
  ['ui.statusline.vline'] = mode(c.orange),
  ['ui.statusline.replace'] = mode(c.red),
  ['ui.statusline.command'] = mode(c.green),
  ['ui.statusline.search'] = mode(c.yellow),
  ['ui.statusline.files'] = mode(c.aqua),
  ['ui.statusline.file'] = { fg = c.fg1, bg = c.bg2 },
  ['ui.statusline.modified'] = { fg = c.orange },
  ['ui.statusline.lines'] = { fg = c.fg3, bg = c.bg2 },
  ['ui.statusline.filetype'] = { fg = c.fg3, bg = c.bg2 },
  ['ui.statusline.position'] = { fg = c.bg0_h, bg = c.fg4, bold = true },
  ['ui.statusline.percent'] = { fg = c.fg1, bg = c.bg3 },

  -- file tree
  ['ui.tree.file'] = { fg = c.fg1 },
  ['ui.tree.dir'] = { fg = c.blue, bold = true },
  ['ui.tree.root'] = { fg = c.yellow, bold = true },
  ['ui.tree.open'] = { fg = c.aqua, bold = true },
  ['ui.tree.active'] = { fg = c.yellow, bold = true },
  ['ui.tree.hidden'] = { fg = c.bg4 },
  ['ui.tree.guide'] = { fg = c.bg2 },
  ['ui.tree.sep'] = { fg = c.bg2 },
  ['ui.tree.select'] = { bg = c.bg1, bold = true },
  ['ui.tree.match'] = { fg = c.orange, undercurl = true },
  ['ui.tree.symlink'] = { fg = c.aqua, italic = true },
  ['ui.tree.icon.dir'] = { fg = c.yellow_d },
  ['ui.tree.icon.file'] = { fg = c.fg4 },
  ['ui.tree.git.modified'] = { fg = c.yellow },
  ['ui.tree.git.added'] = { fg = c.green },
  ['ui.tree.git.deleted'] = { fg = c.red },
  ['ui.tree.git.untracked'] = { fg = c.aqua },
  ['ui.tree.git.conflict'] = { fg = c.orange },
  ['ui.tree.git.staged'] = { fg = c.green },

  -- menus, pickers, floats, prompts
  ['ui.menu'] = { fg = c.fg1, bg = c.bg1 },
  ['ui.menu.select'] = { bg = c.bg3, bold = true },
  ['ui.menu.marker'] = { fg = c.yellow },
  ['ui.menu.kind'] = { fg = c.purple, bold = true },
  ['ui.menu.detail'] = { fg = c.gray },
  ['ui.menu.dir'] = { fg = c.fg4 },
  ['ui.menu.match'] = { fg = c.orange, bold = true },
  ['ui.menu.disabled'] = { fg = c.bg4 },
  ['ui.menu.doc'] = { fg = c.fg3 },
  ['ui.menu.key'] = { fg = c.yellow, bold = true },
  ['ui.float'] = { fg = c.fg1, bg = c.bg1 },
  ['ui.float.border'] = { fg = c.gray, bg = c.bg1 },
  ['ui.float.title'] = { fg = c.yellow, bold = true },
  ['ui.hover'] = { fg = c.fg1, bg = c.bg1 },
  ['ui.picker.border'] = { fg = c.yellow },
  ['ui.picker.prompt'] = { fg = c.orange, bold = true },
  ['ui.input'] = { fg = c.fg1 },
  ['ui.input.border'] = { fg = c.yellow },
  ['ui.input.prompt'] = { fg = c.orange, bold = true },

  -- diagnostics: severity colours; virtual text keeps the severity colour
  ['diag.error'] = { fg = c.red },
  ['diag.warn'] = { fg = c.yellow },
  ['diag.info'] = { fg = c.blue },
  ['diag.hint'] = { fg = c.aqua },
  ['diag.virtual'] = { italic = true },
  ['diag.underline'] = { undercurl = true },
  ['diag.message'] = { fg = c.fg1 },
}

winter.theme.apply(syntax)
winter.theme.apply(lsp)
winter.theme.apply(ui)

-- rainbow brackets: red, orange, yellow, green, aqua, blue, purple
winter.theme.rainbow({ c.red, c.orange, c.yellow, c.green, c.aqua, c.blue, c.purple })
