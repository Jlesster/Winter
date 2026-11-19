-- rosé pine for winter
--
-- add to init.lua:
--     require("rosepine")
--
-- Layout matches gruvbox.lua: tree-sitter captures, lsp semantic tokens, ui chrome.
-- Colours live in `c`; how they map onto syntax roles lives in `s`, so
-- restyling the theme is a matter of editing `s`.
-- Also styles the statusline components of the example init.lua (branch,
-- selection, clock, ruler) and the build-output / markdown groups.
-- Rosé Pine has no orange or green of its own: the `orange` slot holds rose and
-- `green` holds foam, `blue` is the moon-pine blue (readable on the base).

local c = {
  bg0_h = '#12101c',
  bg0 = '#191724',
  bg1 = '#1f1d2e',
  bg2 = '#26233a',
  bg3 = '#403d52',
  bg4 = '#6e6a86',
  sel = '#403d52',
  fg0 = '#e0def4',
  fg1 = '#e0def4',
  fg2 = '#cac7e0',
  fg3 = '#908caa',
  fg4 = '#7a7694',
  gray = '#6e6a86',
  red = '#eb6f92',
  green = '#9ccfd8',
  yellow = '#f6c177',
  blue = '#3e8fb0',
  purple = '#c4a7e7',
  aqua = '#9ccfd8',
  orange = '#ebbcba',
  red_d = '#c4577a',
  yellow_d = '#d7a35e',
}

local s = {
  accent = c.purple,
  kw = { fg = c.blue },
  storage = { fg = c.blue },
  inc = { fg = c.blue },
  macro = { fg = c.purple },
  str = { fg = c.yellow },
  esc = { fg = c.blue },
  regex = { fg = c.purple },
  special = { fg = c.aqua },
  url = { fg = c.purple, underline = true },
  num = { fg = c.yellow },
  const = { fg = c.yellow },
  fn = { fg = c.orange },
  fnb = { fg = c.red },
  ctor = { fg = c.aqua },
  type = { fg = c.aqua },
  attr = { fg = c.purple },
  ns = { fg = c.aqua },
  label = { fg = c.aqua },
  prop = { fg = c.aqua },
  param = { fg = c.purple, italic = true },
  builtin = { fg = c.red },
  op = { fg = c.fg3 },
  punct = { fg = c.fg4 },
  comment = { fg = c.gray, italic = true },
  tag = { fg = c.aqua },
  tagattr = { fg = c.purple },
  heading = { fg = c.orange, bold = true },
  link = { fg = c.aqua, underline = true },
  raw = { fg = c.yellow },
  list = { fg = c.orange },
  m_normal = c.purple, m_insert = c.green, m_visual = c.orange, m_replace = c.red,
  m_command = c.yellow, m_search = c.blue, m_files = c.aqua,
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
winter.theme.rainbow({ c.red, c.orange, c.yellow, c.green, c.blue, c.purple, '#e0def4' })
