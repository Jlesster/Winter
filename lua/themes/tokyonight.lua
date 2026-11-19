-- tokyonight (night) for winter
--
-- add to init.lua:
--     require("tokyonight")
--
-- Layout matches gruvbox.lua: tree-sitter captures, lsp semantic tokens, ui chrome.
-- Colours live in `c`; how they map onto syntax roles lives in `s`, so
-- restyling the theme is a matter of editing `s`.
-- Also styles the statusline components of the example init.lua (branch,
-- selection, clock, ruler) and the build-output / markdown groups.

local c = {
  bg0_h = '#16161e',
  bg0 = '#1a1b26',
  bg1 = '#1f2335',
  bg2 = '#292e42',
  bg3 = '#3b4261',
  bg4 = '#545c7e',
  sel = '#283457',
  fg0 = '#c8d3f5',
  fg1 = '#c0caf5',
  fg2 = '#a9b1d6',
  fg3 = '#9aa5ce',
  fg4 = '#737aa2',
  gray = '#565f89',
  red = '#f7768e',
  green = '#9ece6a',
  yellow = '#e0af68',
  blue = '#7aa2f7',
  purple = '#bb9af7',
  aqua = '#7dcfff',
  orange = '#ff9e64',
  red_d = '#db4b4b',
  yellow_d = '#c49a56',
}

local s = {
  accent = c.blue,
  kw = { fg = c.purple, italic = true },
  storage = { fg = c.purple },
  inc = { fg = c.aqua },
  macro = { fg = c.aqua },
  str = { fg = c.green },
  esc = { fg = c.purple },
  regex = { fg = '#b4f9f8' },
  special = { fg = '#1abc9c' },
  url = { fg = c.aqua, underline = true },
  num = { fg = c.orange },
  const = { fg = c.orange },
  fn = { fg = c.blue },
  fnb = { fg = '#2ac3de' },
  ctor = { fg = c.purple },
  type = { fg = '#2ac3de' },
  attr = { fg = c.aqua },
  ns = { fg = c.aqua },
  label = { fg = c.blue },
  prop = { fg = '#73daca' },
  param = { fg = c.yellow, italic = true },
  builtin = { fg = c.red },
  op = { fg = '#89ddff' },
  punct = { fg = '#89ddff' },
  comment = { fg = c.gray, italic = true },
  tag = { fg = c.red },
  tagattr = { fg = c.orange },
  heading = { fg = c.blue, bold = true },
  link = { fg = c.purple, underline = true },
  raw = { fg = '#73daca' },
  list = { fg = c.orange },
  m_normal = c.blue, m_insert = c.green, m_visual = c.purple, m_replace = c.red,
  m_command = c.yellow, m_search = c.orange, m_files = '#73daca',
}

local function with(base, extra)
  local t = {}
  for k, v in pairs(base) do t[k] = v end
  for k, v in pairs(extra) do t[k] = v end
  return t
end

-- ---------------------------------------------------------------------------
-- 1. tree-sitter captures
-- ---------------------------------------------------------------------------
local syntax = {
  keyword = s.kw,
  conditional = s.kw,
  ['repeat'] = s.kw,
  exception = s.kw,
  storageclass = s.storage,
  include = s.inc,
  preproc = s.inc,
  define = s.inc,
  ['keyword.directive'] = s.inc,
  ['function.macro'] = s.macro,

  string = s.str,
  character = s.str,
  ['string.escape'] = s.esc,
  escape = s.esc,
  ['string.regexp'] = s.regex,
  ['string.special'] = s.special,
  ['string.special.url'] = s.url,
  ['character.special'] = s.esc,

  number = s.num,
  float = s.num,
  boolean = s.num,
  constant = s.const,

  ['function'] = s.fn,
  method = s.fn,
  ['function.builtin'] = s.fnb,
  constructor = s.ctor,

  type = s.type,
  attribute = s.attr,
  namespace = s.ns,
  module = s.ns,
  label = s.label,

  property = s.prop,
  field = s.prop,
  ['variable.member'] = s.prop,
  ['variable.parameter'] = s.param,
  parameter = s.param,
  ['variable.builtin'] = s.builtin,

  -- operators (including `&`, `*`, `&&` in parameter lists) share one colour
  operator = s.op,
  punctuation = s.punct,

  comment = s.comment,
  ['comment.error'] = { fg = c.red, bold = true },
  ['comment.warning'] = { fg = c.yellow, bold = true },
  ['comment.todo'] = { fg = c.blue, bold = true },
  ['comment.note'] = { fg = c.blue, bold = true },

  tag = s.tag,
  ['tag.attribute'] = s.tagattr,
  ['tag.delimiter'] = s.punct,

  ['markup.heading'] = s.heading,
  ['markup.strong'] = { bold = true },
  ['markup.italic'] = { italic = true },
  ['markup.strikethrough'] = { strikethrough = true },
  ['markup.link'] = s.link,
  ['markup.link.url'] = s.url,
  ['markup.raw'] = s.raw,
  ['markup.list'] = s.list,
  ['markup.quote'] = s.comment,

  ['diff.plus'] = { fg = c.green },
  ['diff.minus'] = { fg = c.red },
  ['diff.delta'] = { fg = c.yellow },
}

-- ---------------------------------------------------------------------------
-- 2. lsp semantic tokens
-- ---------------------------------------------------------------------------
local lsp = {
  ['lsp.namespace'] = s.ns,
  ['lsp.type'] = s.type,
  ['lsp.class'] = s.type,
  ['lsp.struct'] = s.type,
  ['lsp.enum'] = s.type,
  ['lsp.interface'] = s.type,
  ['lsp.concept'] = with(s.ns, { bold = true }),
  ['lsp.typeParameter'] = with(s.type, { italic = true }),

  ['lsp.variable'] = { fg = c.fg1 },
  ['lsp.parameter'] = s.param,
  ['lsp.property'] = s.prop,
  ['lsp.enumMember'] = s.const,

  ['lsp.function'] = s.fn,
  ['lsp.method'] = s.fn,
  ['lsp.macro'] = s.macro,
  ['lsp.decorator'] = s.attr,
  ['lsp.label'] = s.label,

  -- extra vocabulary from rust-analyzer / kotlin / lua servers
  ['lsp.typeAlias'] = s.type,
  ['lsp.union'] = s.type,
  ['lsp.trait'] = with(s.type, { italic = true }),
  ['lsp.generic'] = with(s.type, { italic = true }),
  ['lsp.builtinType'] = s.type,
  ['lsp.selfKeyword'] = with(s.kw, { italic = true }),
  ['lsp.selfTypeKeyword'] = with(s.type, { italic = true }),
  ['lsp.lifetime'] = with(s.label, { italic = true }),
  ['lsp.derive'] = s.attr,
  ['lsp.deriveHelper'] = s.attr,
  ['lsp.builtinAttribute'] = s.attr,
  ['lsp.attribute'] = s.attr,
  ['lsp.escapeSequence'] = s.esc,
  ['lsp.formatSpecifier'] = s.esc,
  ['lsp.boolean'] = s.num,

  -- modifiers add attributes only (no fg), so they never recolour a token.
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
  ['ui.linenr.cursor'] = { fg = s.accent, bold = true },
  ['ui.cursor'] = { fg = c.bg0_h, bg = c.fg1 },
  ['ui.cursorline'] = { bg = c.bg1 },
  ['ui.colorcolumn'] = { bg = c.bg1 },
  ['ui.matchparen'] = { bg = c.bg3, bold = true },
  ['ui.trailing'] = { bg = c.red_d },
  ['ui.indent_lead'] = { fg = c.bg2 },
  ['ui.wrap'] = { fg = c.gray },

  ['ui.visual'] = { bg = c.sel },
  ['ui.search'] = { fg = c.bg0_h, bg = c.yellow_d },
  ['ui.search.current'] = { fg = c.bg0_h, bg = c.orange, bold = true },
  ['ui.search.cursor'] = { fg = c.bg0_h, bg = c.red, bold = true },
  ['ui.subst.match'] = { fg = c.bg0_h, bg = c.red },
  ['ui.subst.replace'] = { fg = c.bg0_h, bg = c.green },

  -- lsp document highlight + snippets + multicursor
  ['ui.reference'] = { bg = c.bg2 },
  ['ui.reference.read'] = { bg = c.bg2 },
  ['ui.reference.write'] = { bg = c.bg3, underline = true },
  ['ui.snippet'] = { bg = c.bg2 },
  ['ui.snippet.active'] = { bg = c.bg3, underline = true },
  ['ui.multicursor'] = { fg = c.bg0_h, bg = s.accent },
  ['ui.multicursor.select'] = { bg = c.sel },

  -- status line
  ['ui.statusline'] = { fg = c.fg3, bg = c.bg1 },
  ['ui.statusline.nc'] = { fg = c.bg4, bg = c.bg1 },
  ['ui.statusline.normal'] = mode(s.m_normal),
  ['ui.statusline.insert'] = mode(s.m_insert),
  ['ui.statusline.visual'] = mode(s.m_visual),
  ['ui.statusline.vline'] = mode(s.m_visual),
  ['ui.statusline.vfiles'] = mode(s.m_visual),
  ['ui.statusline.replace'] = mode(s.m_replace),
  ['ui.statusline.command'] = mode(s.m_command),
  ['ui.statusline.search'] = mode(s.m_search),
  ['ui.statusline.files'] = mode(s.m_files),
  ['ui.statusline.file'] = { fg = c.fg1, bg = c.bg2 },
  ['ui.statusline.modified'] = { fg = c.orange },
  ['ui.statusline.lines'] = { fg = c.fg3, bg = c.bg2 },
  ['ui.statusline.buffers'] = { fg = c.fg3, bg = c.bg2 },
  ['ui.statusline.filetype'] = { fg = c.fg3, bg = c.bg2 },
  ['ui.statusline.indent'] = { fg = c.fg3, bg = c.bg2 },
  ['ui.statusline.position'] = { fg = c.bg0_h, bg = c.fg4, bold = true },
  ['ui.statusline.percent'] = { fg = c.fg1, bg = c.bg3 },
  -- components defined by the example init.lua
  ['ui.statusline.branch'] = { fg = c.green, bg = c.bg2 },
  ['ui.statusline.selection'] = mode(s.m_visual),
  ['ui.statusline.clock'] = { fg = c.bg0_h, bg = s.accent, bold = true },
  ['ui.statusline.ruler'] = { fg = c.fg1, bg = c.bg2 },

  -- file tree
  ['ui.tree.file'] = { fg = c.fg1 },
  ['ui.tree.dir'] = { fg = c.blue, bold = true },
  ['ui.tree.root'] = { fg = s.accent, bold = true },
  ['ui.tree.open'] = { fg = c.aqua, bold = true },
  ['ui.tree.active'] = { fg = s.accent, bold = true },
  ['ui.tree.hidden'] = { fg = c.bg4 },
  ['ui.tree.guide'] = { fg = c.bg2 },
  ['ui.tree.sep'] = { fg = c.bg2 },
  ['ui.tree.select'] = { bg = c.bg2, bold = true },
  ['ui.tree.visual'] = { bg = c.sel },
  ['ui.tree.match'] = { fg = c.orange, undercurl = true },
  ['ui.tree.symlink'] = { fg = c.aqua, italic = true },
  ['ui.tree.icon.dir'] = { fg = c.blue },
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
  ['ui.menu.marker'] = { fg = s.accent },
  ['ui.menu.kind'] = { fg = c.purple, bold = true },
  ['ui.menu.detail'] = { fg = c.gray },
  ['ui.menu.dir'] = { fg = c.fg4 },
  ['ui.menu.match'] = { fg = c.orange, bold = true },
  ['ui.menu.disabled'] = { fg = c.bg4 },
  ['ui.menu.doc'] = { fg = c.fg3 },
  ['ui.menu.key'] = { fg = c.yellow, bold = true },
  ['ui.float'] = { fg = c.fg1, bg = c.bg1 },
  ['ui.float.border'] = { fg = c.gray, bg = c.bg1 },
  ['ui.float.title'] = { fg = s.accent, bold = true },
  ['ui.hover'] = { fg = c.fg1, bg = c.bg1 },
  ['ui.hover.code'] = { fg = c.green },
  ['ui.hover.heading'] = { fg = s.accent, bold = true },
  ['ui.picker.border'] = { fg = s.accent },
  ['ui.picker.prompt'] = { fg = c.orange, bold = true },
  ['ui.input'] = { fg = c.fg1, bg = c.bg1 },
  ['ui.input.border'] = { fg = s.accent },
  ['ui.input.prompt'] = { fg = c.orange, bold = true },

  -- diagnostics: severity colours; virtual text keeps the severity colour
  ['diag.error'] = { fg = c.red },
  ['diag.warn'] = { fg = c.yellow },
  ['diag.info'] = { fg = c.blue },
  ['diag.hint'] = { fg = c.aqua },
  ['diag.virtual'] = { italic = true },
  ['diag.underline'] = { undercurl = true },
  ['diag.message'] = { fg = c.fg1 },

  -- build / run output panel
  ['output.title'] = { fg = s.accent, bold = true },
  ['output.ok'] = { fg = c.green },
  ['output.fail'] = { fg = c.red, bold = true },
  ['output.error'] = { fg = c.red },
  ['output.warning'] = { fg = c.yellow },
  ['output.note'] = { fg = c.blue },
  ['output.running'] = { fg = c.orange },
  ['output.dim'] = { fg = c.gray },

  -- markdown buffers
  ['md.marker'] = { fg = c.bg4 },
  ['md.bullet'] = { fg = c.orange },
  ['md.link'] = s.link,
  ['md.link.url'] = s.url,
  ['md.quote'] = s.comment,
  ['md.rule'] = { fg = c.bg3 },
  ['md.code'] = s.raw,
  ['md.code.block'] = { bg = c.bg0_h },
  ['md.code.fence'] = { fg = c.bg4 },
  ['md.code.lang'] = { fg = c.purple },
  ['md.table.border'] = { fg = c.bg4 },
  ['md.table.head'] = { fg = s.accent, bold = true },
  ['md.checkbox.unchecked'] = { fg = c.gray },
  ['md.checkbox.checked'] = { fg = c.green },
  ['md.checkbox.progress'] = { fg = c.yellow },
  ['md.checkbox.cancelled'] = { fg = c.red },
}

winter.theme.apply(syntax)
winter.theme.apply(lsp)
winter.theme.apply(ui)

-- rainbow brackets
winter.theme.rainbow({ c.red, c.orange, c.yellow, c.green, '#73daca', c.blue, c.purple })
