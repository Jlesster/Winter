<h1 align="center">❄️ winter</h1>

<p align="center">
  <img src="https://img.shields.io/badge/language-C++23-8839ef?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++23" />
  <img src="https://img.shields.io/badge/license-MIT-939ab7?style=for-the-badge&logo=opensourceinitiative&logoColor=white" alt="MIT License" />
  <img src="https://img.shields.io/badge/build-meson-f5c2e7?style=for-the-badge&logo=meson&logoColor=white" alt="Meson" />
  <img src="https://img.shields.io/badge/platform-linux-fab387?style=for-the-badge&logo=linux&logoColor=white" alt="Linux" />
  <img src="https://img.shields.io/badge/status-active-a6e3a1?style=for-the-badge" alt="Active" />
</p>

<p align="center">
  <strong>A modal terminal editor with a real IDE inside it.</strong><br/>
  <em>Vim's grammar, tree-sitter and LSP built into the core, and Lua over all of it.<br/>
  One static-feeling binary, no plugin manager required.</em>
</p>

---

## What is winter?

`winter` is a from-scratch vi-style editor written in C++23 (about 44k lines, no
editor framework underneath). Everything you would normally assemble from a
plugin pile is part of the editor itself: a language-server client, a
tree-sitter engine with runtime-installable grammars, a file tree, fuzzy
pickers, multiple cursors, an integrated terminal, a build runner with a
quickfix list, and a Lua API that exposes the lot.

It is built around three ideas:

- **Everything is an action.** Every key, command and menu entry resolves to a
  named action. Keys are just data pointing at action names, and Lua uses the
  same tables.
- **Async data never fights the text.** Diagnostics, semantic tokens, anchors
  and decorations are tied to the piece table and follow edits, instead of
  being re-derived after the fact.
- **Fast by construction.** A piece table, diffed cell rendering and
  incremental parsing keep large files and heavy decoration cheap.

---

## ✨ Features

### 🚀 Core engine

- **Piece table buffer** with cheap inserts/deletes at any size, minimal-hunk
  whole-buffer replacement (so a format or reload becomes small edits, not a
  reset), and persistent **undo files** (`undofile`, `undolevels`).
- **Double-buffered cell renderer**: a virtual grid diffed into the smallest
  possible VT sequence stream, flushed with **synchronized output** (DEC mode
  2026) for tear-free frames.
- **UTF-8 native**: grapheme clusters, wide characters and tab/control-aware
  column math throughout.
- **Soft wrap** with wrap indicator, folding, sign/number gutter, relative
  numbers, cursor line, color column, matching-pair and trailing-whitespace
  highlights.
- **Windows**: `:split`, `:vsplit`, `:new`, `:vnew`, `:only`, `CTRL-W`
  navigation and resizing, plus **tmux pane navigation** that carries on past
  the edge of the editor.
- **Hot reload**: inotify watches your config (`hotreload`) and open files, so
  edits on disk show up without restarting.

### 🌳 Tree-sitter

- **Runtime grammars.** `c`, `cpp`, `java`, `lua` and `rust` are compiled in;
  `:TSInstall <lang>` fetches and builds any of the 300+ grammars from the
  nvim-treesitter parser list, and `:TSUpdate` refreshes the list and rebuilds
  outdated ones.
- **Highlighting, query-driven indentation and folding** from tree-sitter
  queries, with a selectable indent engine (`indent_engine`).
- **Layered file-type detection**: exact file name, name prefix, longest
  extension, installed-grammar metadata, then synced GitHub linguist data. Use
  `:ft` to inspect or override.
- **Scriptable**: `winter.ts` exposes queries (with `#eq?`, `#match?`,
  `#any-of?` and their negations), captures, `root` and `node_at`.

### 🧠 Language Server Protocol

A full client, not a hover-and-diagnostics demo.

| Area | What you get |
| --- | --- |
| **Navigation** | Definition, declaration, implementation, type definition, references (`gd`, `gD`, `gi`, `gy`, `gr`), header/implementation switch for clangd, all feeding the shared jumplist |
| **Completion** | Insert-mode popup with snippet expansion (multi-cursor aware), lazy `resolve` for docs/detail/auto-imports, colored kind icons |
| **Hover** | `K` opens a float rendered from markdown (headings, code blocks, lists, concealed syntax) |
| **Refactoring** | Rename with `prepareRename` (`<space>lr`), code actions with resolve and server commands (`<space>la`, also on a visual selection), full `WorkspaceEdit` support including create/rename/delete |
| **Formatting** | `:format`, optional format-on-save with a timeout that never blocks a save |
| **Diagnostics** | Virtual text, undercurls, severity filtering, `[d` / `]d`, and a picker for the buffer or the whole workspace (`:Diagnostics[!]`) |
| **Highlights** | Document-highlight of references under the cursor (`hlreference`) |
| **Semantic tokens** | Full and **delta** updates, a `workspace/semanticTokens/refresh` handler, request supersession with `$/cancelRequest`, bulk span replacement, and per-type / per-modifier theming (see below) |
| **Sync** | Incremental `didChange`, UTF-8 or UTF-16 position encodings negotiated per server, per-server enable/start/stop/restart |

Servers are configured from Lua, and can reuse nvim-lspconfig's
`lsp/<name>.lua` server definitions. Use `:lsp start|stop|restart <server>`
to control them and `:semstat` to see exactly what the semantic token pipeline
is doing for the current buffer.

#### Semantic token theming

Server tokens are mapped onto your tree-sitter highlight groups by default
(`class` → `type`, `enumMember` → `constant`, `macro` → `define`, ...), and
every step is overridable:

```lua
winter.theme.set("lsp.parameter",       { fg = "#eba0ac", italic = true })
winter.theme.set("lsp.function.static", { italic = true })   -- type.modifier
winter.theme.set("lsp.mod.deprecated",  { strikethrough = true })
```

`defaultLibrary` and `readonly` get sensible built-in styling, and `deprecated`
is struck through, so most themes need no extra work.

### 🛠️ Workflow

- **Modal editing** with the grammar you expect: operators, motions, text
  objects, counts, dot-repeat, vim-style **registers** (`"`, `0`-`9`, `-`,
  `a`-`z`/`A`-`Z`, `_`, and the system clipboard through `+` / `*`),
  **marks** (`a-z` follow edits, `A-Z` persist across files), the **jumplist**
  (`<C-o>` / `<C-i>`), `Ctrl-A` / `Ctrl-X` number bumping with configurable
  formats, and `:s` with a **live preview** that highlights matches as you
  type and then shows the replacement in the buffer.
- **Multiple cursors** that behave like first-class citizens: each cursor has
  its own registers, edits replay per cursor, snippets expand at every cursor,
  and there are commands to skip, add all matches, remove the last, step
  through, and split a selection into lines. `g<C-a>` increments progressively
  per cursor.
- **Fuzzy pickers** on one shared matcher and menu widget: `:Files`, `:Grep`
  (live, ripgrep-backed), `:Recent`, `:Diagnostics`, `:marks`. The command
  line itself has a completion popup with prefix matching.
- **File tree** (`<space>e`): a neo-tree-style sidebar with git status,
  file-type icons, follow-current-file, hidden-file toggling, an in-tree input
  box, an action palette and recursive search.
- **Build runner**: `:make` plus `:meson`, `:cmake`, `:cargo`, `:go` and
  `:gradle`, with the project root detected from marker files. Output goes
  through error parsers into the **quickfix list**, and is shown in a bottom
  dock, a floating pane, or a full buffer (so every motion, search and yank
  still works on build logs).
- **Integrated terminal** (`:Term`): a real PTY with a built-in VT emulator,
  docked or floating. `<C-\><C-n>` returns to the editor.
- **Markdown rendering** for hover docs and markdown buffers, with
  `:MarkdownReveal` to see the raw line under the cursor.
- **Shell integration**: `:!cmd`, and with a range it becomes a filter.

### 🧩 Lua scripting

Your config lives in `~/.config/winter/init.lua` (honours `XDG_CONFIG_HOME`).
The Lua runtime is embedded in the binary, so there is nothing to install, and
`:reload` re-runs your config without restarting. Plugins load from the
`plugins/` directory next to it.

```lua
-- options
winter.opt.relativenumber = true
winter.opt.formatonsave = true

-- keymaps: modes as letters ("nx"), words, or tables; action name or function
winter.keymap.set("n", "<space>t", "tree.toggle", { desc = "file tree" })
winter.keymap.set({ "n", "x" }, "<A-j>", function() winter.notify("hi") end)

-- user commands with completion, ranges, bang and arg counts
winter.cmd.add("Greet", function(a) winter.notify("hello " .. a.args) end,
               { nargs = "1", desc = "say hello" })

-- events
winter.ev.on("buf_write_post", function(e) winter.notify("saved " .. e.filename) end)

-- language servers
winter.lsp("clangd", { enabled = true })

-- theme
winter.theme.apply({ ["function"] = { fg = "#89b4fa" } })
winter.theme.rainbow({ "#f38ba8", "#fab387", "#f9e2af", "#a6e3a1", "#89b4fa" })
```

The API covers buffers by stable id (`winter.api`, buffer handles), windows,
registers, marks, timers, jobs and `system`, `winter.picker` and `winter.ui`
(select/input), `winter.statusline` (reorder, replace or extend any component),
`winter.fold`, `winter.tree.icons`, `winter.ts`, `winter.lsp.request` and
`supports`, plus `winter.ext` for defining your **own operators, motions and
text objects**.

**Events:** `ready`, `quit`, `reload`, `buf_new`, `buf_enter`, `buf_close`,
`buf_reload`, `buf_write_pre`, `buf_write_post`, `file_type`, `text_changed`,
`cursor_moved`, `mode_changed`, `key`, `yank_post`, `win_new`, `win_enter`,
`win_leave`, `win_close`, `lsp_attach`, `lsp_detach`.

---

## ⌨️ Default keys

| Key | Action |
| --- | --- |
| `K` | LSP hover |
| `gd` / `gD` / `gi` / `gy` / `gr` | Definition / declaration / implementation / type definition / references |
| `<space>lr` | Rename symbol |
| `<space>la` | Code action (normal and visual) |
| `[d` / `]d` | Previous / next diagnostic |
| `<space>e` | Toggle the file tree |
| `<space>O` | Toggle the build/run output |
| `<space>/` | Toggle comment (line, count or visual selection) |
| `<C-o>` / `<C-i>` | Jumplist back / forward |
| `<C-a>` / `<C-x>` | Increment / decrement the number under the cursor |
| `zo` `zc` `za` `zR` `zM` | Fold open / close / toggle / open all / close all |
| `<C-w>` + `h` `j` `k` `l` | Window navigation (continues into tmux) |

Everything is rebindable with `winter.keymap.set`, and `winter.keymap.list`
shows what is bound.

---

## 🏗️ Architecture

`winter` keeps strict layers so each can be understood, and tested, alone:

1. **Document model**: the `PieceTable` handles raw text, edits, anchors and
   undo without knowing about a screen. Decorations, diagnostics, marks,
   cursors and folds all hang off it as anchors that follow every edit.
2. **Services**: tree-sitter, the LSP client, formatters and job runners. They
   speak to the buffer through small edit hunks, and async results (semantic
   tokens, diagnostics) are version-checked so stale data is dropped rather
   than painted.
3. **Actions and input**: the registry of named actions that keys, commands,
   operators and Lua all share.
4. **Cell grid and flush**: a virtual grid of characters and styles (`front` vs
   `back`) and a diffing engine that translates changes into the most efficient
   VT sequences possible.

---

## 🚀 Getting started

### Requirements

- **Linux** (uses inotify and PTYs).
- A C++23 compiler with `<format>` and ranges: GCC 13+ or a recent Clang.
- [`meson`](https://mesonbuild.com) and `ninja`.
- [`tree-sitter`](https://tree-sitter.github.io) development libraries.
- Lua 5.4 development libraries.
- `trux`, the terminal toolkit winter renders with.
- **Runtime extras:** `ripgrep` for `:Grep`; `git`, `curl` and a C/C++ compiler
  for `:TSInstall` (grammars that only ship `grammar.js` also need the
  `tree-sitter` CLI and `node`); and a [Nerd Font](https://www.nerdfonts.com)
  if you want file icons (`nerdicons`).

### Build

```bash
git clone https://github.com/Jlesster/winter.git
cd winter
meson setup builddir
ninja -C builddir
./builddir/winter
```

### First run

```text
:TSInstall python        " grammar for the language you work in
:lsp start clangd        " or any server you have configured
:Files                   " fuzzy-open a file
<space>e                 " toggle the file tree
:Term                    " drop into a shell without leaving
```

---

## 📜 Roadmap

- [x] 📂 File explorer / project tree
- [x] 🧩 Lua plugin API
- [x] 🔌 LSP client (navigation, completion, rename, code actions, formatting)
- [x] 🎨 Semantic tokens with delta updates
- [x] 🔍 Fuzzy pickers and live grep
- [x] ✂️ Multiple cursors
- [x] 🖥️ Integrated terminal and build runner
- [~] 🌗 Advanced theme engine
- [~] ⌨️ Advanced key-chord support
- [ ] ⚡ `semanticTokens/range` for faster first paint on very large files

---

<p align="center">
  Made with ❤️ for the terminal community.
</p>
