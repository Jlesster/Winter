# Changelog

All notable changes to winter are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/) (pre-1.0: minor bumps may break things).

## [0.1.3] - 2026-11-26

Documentation grows Java: `lwjgl` and any other Javadoc-backed docset can be
built from jars your build tools already downloaded, `:DocRemove` removes
installed docsets, and `:DocInstall` completion no longer stalls the editor.
Editing gains `J`, a digit-wise `A-a` / `A-x`, and a number-conversion popup on
`K`.

### Added

#### Offline documentation
- **`:DocRemove {slug...}`** deletes installed docsets. A base name such as
  `lua` works when exactly one version is installed; with several it refuses
  and lists them, so removal never guesses. Every argument is resolved before
  anything is deleted, so a typo in the second does not half-apply the first.
  The recent-lookups list and `index.md` are updated, and a doc split showing a
  removed page falls back to `index.md`. It is refused while an install or
  update job is running. Tab completes installed slugs, versions included.
- **Javadoc docsets.** A new `JAVADOC` row kind in `src/docsets.inc` builds a
  docset from `-javadoc.jar` files, merging one jar per `group:artifact` into
  one docset. Jars your build tools already fetched are used first (the Maven
  repository from `settings.xml` or `~/.m2/repository`, `/usr/share/maven-repo`,
  the Gradle cache, `/usr/share/javadoc`); only when none is found does it
  download from Maven Central. Each version is its own docset (`lwjgl~3.3.6`):
  `:DocInstall lwjgl` takes the newest found, `lwjgl~3.3.6` picks one, and
  `:DocUpdate` compares versions.
- **`lwjgl` is now installable** (core, GLFW, OpenGL, OpenAL and STB modules)
  and is searched first for `java` and `kotlin` files. LWJGL's core-profile
  twin classes (`GL11C`, `GL45C`, ...) keep their pages but no longer shadow the
  documented members of `GL11`, `GL45`, ...
- `:DocInstall <Tab>` offers the local Javadoc versions it finds (marked
  `local Javadoc`), and a base-name row shows how many versions exist.
- `winter --doc-locate <slug>`: prints the version and the jars or directories
  of local Javadoc for a slug. Exit status 1 means nothing was found locally.

#### Editing
- **`J` joins lines**, in normal mode (a count joins that many lines, minimum
  two) and in visual mode (the selected lines). Leading whitespace of the next
  line and trailing whitespace of the current one are dropped, and the glue is
  code-aware: no space after `(`, `[`, `->` or `::`, nor before `)`, `]`, `,`,
  `;`, `->`, `::` or a `.name` member access. The cursor lands on the join, and
  the whole join is one undo step.
- **`A-a` / `A-x` bump the single digit under the cursor**, wrapping `9` <-> `0`
  without carrying: on the `6` of `861` they give `871` / `851`, on the `9` of
  `891` they give `801`. A count adds that many steps. Nothing happens when the
  cursor is not on a digit.

#### Hover
- **`K` on a hex, binary or octal literal** (`0xFF`, `0b101`, `0o755`, `0755`)
  shows the value in every base instead of asking the language server: dec,
  hex, bin and oct, plus a signed `iN` reading when the top bit is set and
  `f32` / `f64` when the digits fill that width exactly. A run of four or more
  `0`/`1` digits with leading zeros (`00000111`) reads as binary, with its C
  octal value on the last row. Decimals, reals and versions still go to the
  language server.

### Changed
- **Normal-mode `C-a` / `C-x` now bump the decimal digit run under (or after)
  the cursor**, which is what `A-a` / `A-x` did before: `1.2.3` -> `1.3.3`,
  `3.99` -> `4.00`. Visual-mode `C-a` / `C-x` and `g C-a` / `g C-x` are
  unchanged.
- **Action names.** `edit.increment` and `edit.decrement` carry that behaviour
  now, and `edit.increment_int` / `edit.decrement_int` are gone, replaced by
  `edit.increment_digit` / `edit.decrement_digit` on `A-a` / `A-x`. A config that
  binds these by action name needs updating.

### Fixed
- **Lag in `:DocInstall <Tab>`.** Every keystroke re-parsed `catalog.json`,
  re-read every installed `meta.json` once per candidate, and re-walked the
  Maven and Gradle caches. The catalogue, the installed set, the local Javadoc
  scan (refreshed at most every five minutes) and the candidate list are now
  cached and invalidated by file modification time, so a keystroke only filters
  a ready list.
- Installing a docset no longer discards every loaded doc index. Each index is
  re-read only when its own `index.tsv` changed.

### Internal
- `WINTER_VERSION` is `0.1.3`.
- `hover_present` builds the `K` popup for both language-server and number
  hovers.
- New library functions: `num::describe`, `doc::remove_docset`,
  `doc::find_javadoc`, `doc::javadoc_for` and `doc::import_javadoc`.
- Formatting-only changes in `builtin_actions.cpp`, `edit.cpp`, `edit.hpp`,
  `doc.cpp`, `doc_core.cpp` and `doc_core.hpp`.

## [0.1.2] - 20/11/2026

Offline documentation arrives: `:Doc` looks up the name under the cursor in
docsets stored on disk, and `:DocInstall` / `:DocUpdate` keep them current. The
`K` popup is rebuilt on real windows, and file-type detection learns to read
dotfile layouts.

### Added

#### Offline documentation
- **`:Doc[!] [name | show [slug]]`** shows the documentation for the name under
  the cursor (or the one you give) in a split, rendered as markdown. Docsets
  for the buffer's file type are searched first; `!` shows the saved HTML copy.
  Inside the split, `K` on a link follows it.
- **`:DocInstall[!] [slug... | all]`** downloads and converts docsets.
  Without arguments it lists everything on offer with `*` on installed ones;
  `all` installs the missing ones from winter's own list, and `!` redoes the
  installed ones too. A base name such as `lua` installs the newest version;
  `lua~5.3` picks one.
- **`:DocUpdate[!] [slug]`** refreshes the installed docsets, or all of them
  without an argument. It also refreshes the list of what DevDocs offers
  (saved as `catalog.json` in the docs directory), even when nothing is
  installed yet. Docsets that are already current are skipped; `!` rebuilds
  anyway.
- **Every DevDocs docset can be installed**, not just the ones winter knows
  about. `:DocInstall <Tab>` completes the whole catalogue, one entry per
  docset; versioned names (`lua~5.4`) only appear once you type the `~`.
- **Library docsets built from upstream markdown:** `nlohmann_json`, `sdl3`
  and `sdl2`. winter downloads the repository at its current branch head,
  converts the pages itself and records the commit, so `:DocUpdate` only
  rebuilds when upstream changed. Needs `git`, `curl` and `tar`.
- **`src/docsets.inc`** is the single list of docsets winter knows about: the
  scope separator and code fence of each, which file types search it first, and
  where the markdown-built ones come from. Adding a library is one row.
- Options `doc_breaks` (one sentence per line when shown, default on),
  `doc_table_width` (tables wider than this become lists, 0 = window width) and
  `save_html` (keep the original HTML when installing).
- `winter --doc-resolve`, `--doc-check` and `--doc-build`: headless entry points
  the install job runs.

#### Hover popup
- **Panes are real windows.** `K` opens the popup, and a second `K` focuses it;
  from there ordinary normal mode works: motions, counts, search, visual mode
  and yank. Only a few keys belong to the popup itself, and every `<C-w>` key
  is handled there so a no-op never leaks into the editor.
- **Links in hover text** are followed from the popup: web and mail links open
  in the desktop's default handler, `file://` links open as a buffer.
- Window, split and buffer commands close the popup first.

#### Markdown
- **Inline formatting** in rendered markdown: `[text](url)` links (underlined,
  and a link that wraps across lines stays one link), bare `http(s)://` URLs,
  `<url>`, `**strong**` and `*emphasis*`, with snake_case left alone.
- **Converter anchor tags** (` {#id}` at the end of a heading or term) are
  hidden in the rendered view but stay in the file so links can jump to them.
- **Version tags** such as `(since C++17)`, `(until C++11)` and `(removed in
  C++20)` are shown as dim italic annotations. Theme key: `md.version`.

#### Language support
- **File-type detection reads more of the file and its place on disk.**
  - Directory rules place files with no (or a generic) extension: `~/.config/zsh/aliases`,
    `tmux/keys.conf`, `fish/functions/x`.
  - A vim modeline (`# vim: set ft=zsh :`), the shebang (including
    `#!/usr/bin/env -S` and `#! nix-shell -i`) and a few shell/tmux-only
    constructs are consulted last.
  - `/nix/store/<hash>-` prefixes and `.local` / `.bak` / `~` suffixes are
    ignored, and a symlink is followed when its own name matched nothing, so
    home-manager and stow managed dotfiles resolve.
  - The head of the buffer used for detection grows from the first line to the
    first 1 KiB.
- **`:ft` explains itself**, e.g. `tmux via directory 'tmux'; parser: tmux`.
- **Automatic parser install** (`ts_autoinstall`, default `1`). When a file
  wants a grammar that isn't built, it is installed in the background, once per
  language per session (a failure is not retried). Until it lands, a close
  grammar highlights the file, such as bash for zsh. Set it to `0` to get the
  old "`:TSInstall`" hint instead.
- Parser installs check for `git`, `curl` and a C compiler up front and say how
  to get the missing one on NixOS and Arch.

#### Editor
- **File tree: `e`** widens the pane to the longest visible name, and again to
  revert. The pane returns to normal when the tree loses focus.
- The picker can start with text already in its input.

### Fixed
- **`gD` (go to declaration)** was registered under the `lsp.definition`
  action name, colliding with `gd`. It is now `lsp.declaration`.
- **Insert with `a`, `A` and `I`** now updates the remembered cursor column, so
  moving up or down straight afterwards uses the column you started typing at.
- **Hover popup panes** no longer show a gutter or indent guides, and don't
  scroll past the end.
- The file picker's preview detected the language from the first line only; it
  now sees the same 1 KiB as the editor.

### Internal
- `WINTER_VERSION` is `0.1.2`.
- `reattach_syntax` lives in `file.cpp` (`buffer_reattach_syntax`) and
  re-resolves each buffer's language, so a rebuilt parser also reaches files
  that were using a fallback.
- Formatting-only changes in `builtin_actions.cpp`, `lsp_actions.cpp`,
  `registry.cpp` and `registry.hpp`.

## [0.1.1] - 2026-11-19

The editor grows from a single-window editor into something you can live in:
splits, an integrated terminal, multiple cursors, registers, marks and folding,
backed by a much larger Lua API and a more complete LSP client.

### Added

#### Editing
- **Multiple cursors.** Add the next match or all matches, add a cursor above or
  below, skip a match, remove the last cursor, step between cursors, and split a
  selection into one cursor per line. Snippets expand at every cursor, and
  completion requests hide the extra cursors' words from the language server so
  the main cursor's context stays intact.
- **Registers.** A proper register module: numbered, named, small-delete and
  black-hole registers, plus the system clipboard through `+` and `*`.
- **Marks.** `a`-`z` marks follow edits, `A`-`Z` marks persist across files.
  `'x` and `` `x `` jump, and also work as operator motions (`d'a`, ``y`a``).
  `:marks` lists them in a picker.
- **Number bumping.** `<C-a>` / `<C-x>` understand binary, hex, octal, real,
  version and identifier-suffix numbers, selected by the new `numformats`
  option (`all`, `none` or a list). Visual `g<C-a>` / `g<C-x>` increment
  progressively.
- **Folding.** Block folding with pluggable providers (tree-sitter queries or
  indentation, configured through `winter.fold.setup`). New options:
  `foldminlines`, `foldlevelstart`, `foldcloser`.
- **Unicode.** Grapheme-cluster segmentation and emoji / wide-character width
  handling for cursor movement and rendering.

#### Windows and panels
- **Splits.** `:split`, `:vsplit`, `:new`, `:vnew`, `:only`, `:close`, `:wincmd`,
  `CTRL-W` navigation and resizing (`resize_step` sets the step).
- **tmux pane navigation.** `CTRL-W h/j/k/l` carries on into neighbouring tmux
  panes at the edge of the editor (`tmux_nav`, `tmux_wrap`).
- **Integrated terminal.** `:Term [toggle|focus|new|next|prev|N|float|bottom|close|kill]`
  runs a real PTY with a built-in VT emulator, docked at the bottom or floating.
- **Build/run output panel.** `:Output [toggle|bottom|float|full|focus|close|kill|rerun|prev|next]`
  and `<space>O`. Show output in a bottom dock, a float, or a full buffer
  (`output_mode`, `output_height`, `output_auto`).
- **Error parsers for `javac` and `gradle`**, so `:gradle` failures land in the
  quickfix list.
- `:notifications` shows the status message history.

#### Language support
- **Layered file-type detection:** exact name, name prefix, longest extension,
  installed-grammar metadata, then synced GitHub linguist data. `:ft [lang]`
  shows or overrides the buffer's language.
- `:TSUpdate[!] [lang]` refreshes the grammar list and rebuilds outdated grammars.
- **`winter.ts`:** tree-sitter syntax trees and queries from Lua.

#### LSP
- **Semantic tokens:** delta updates, a `workspace/semanticTokens/refresh`
  handler, one request in flight per document (newer requests supersede older
  ones, with `$/cancelRequest`), and automatic retry with backoff when a server
  returns nothing yet or cancels. Token types and modifiers can be themed
  (`lsp.<type>`, `lsp.<type>.<modifier>`, `lsp.mod.<modifier>`), with built-in
  styling for `defaultLibrary` and `readonly`.
- **Rename** now asks the server first (`prepareRename`).
- **Workspace diagnostics.** `:Diagnostics[!]` opens a picker for the buffer, or
  for every file with diagnostics when `!` is given, including files that are not
  open.
- **Hover popup** is split into panes (signature, docs, ...) that scroll
  independently; pressing `K` again focuses the popup.
- `:MarkdownReveal` shows the raw markdown under the cursor.

#### Lua API
- New namespaces: `winter.api` (read and edit any buffer by stable id, with a
  change feed and buffer handles), `winter.win`, `winter.term`, `winter.ts`,
  `winter.fold`, `winter.mark`, `winter.reg`, `winter.fs`, `winter.json`,
  `winter.shell`, `winter.errparser`, `winter.plugin`, and `winter.normal` /
  `winter.feedkeys` for driving the editor with key notation.
- An embedded Lua runtime layer (buffer handles, `winter.g` / `winter.b`
  variables, `winter.validate`, `winter.tbl`, `winter.str`, `winter.inspect`).
- New events: `buf_new`, `buf_close`, `yank_post`, `lsp_attach`, `lsp_detach`,
  `win_new`, `win_enter`, `win_leave`, `win_close` and `key`.
- Full reference in [`docs/lua-api.md`](docs/lua-api.md); type stubs in
  `lua/meta/winter.lua` updated to match.

#### Themes
- Bundled `dracula`, `rosepine` and `tokyonight` themes in `lua/themes/`
  (alongside `catppuccin` and `gruvbox`).

#### Packaging and project
- **Nix flake** (`flake.nix`) with a `packages.default` build and a dev shell.
  Tree-sitter and grammar sources are pinned as flake inputs because Meson cannot
  download wraps inside the Nix sandbox.
- README rewritten to cover the full feature set.
- New Meson test suites: `fold`, `vt`, `pty`, `term_keys` and `num`; grapheme
  handling is covered by the existing `utf8` test.

### Changed
- **Install location.** Bundled Lua files install to
  `<prefix>/<sysconfdir>/xdg/winter` instead of a hard-coded `/etc/xdg/winter`,
  and the same path is compiled in so bundled themes are found after install.
  This is what makes installs under a custom prefix (such as Nix) work.
- **Lua detection.** Meson now falls back to the `lua` pkg-config name when
  `lua5.4` is not found. Previously the fallback could never run, because a
  missing `lua5.4` aborted configuration.
- The example `lua/init.lua` is reorganised into labelled sections and gains a
  `greet` knob and the new themes.

### Removed
- **`quit_times` option.** It had no effect in 0.1.0. A config that still sets
  `winter.opt.quit_times` will now fail with `unknown option 'quit_times'`, so
  delete that line.
- The Makefile. Build with Meson (`meson setup builddir && ninja -C builddir`).

### Known issues
- Tests under `tests/lua`, `tests/marks`, `tests/reg`, `tests/ts` and
  `tests/keymap`, plus `md_test` and `grapheme_test`, are not yet registered
  with Meson, so `meson test` does not run them.
- The Nix package builds with `doCheck = false` because `pty_test` needs
  `forkpty`.

[0.1.3]: https://github.com/Jlesster/Winter/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/Jlesster/Winter/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/Jlesster/Winter/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Jlesster/Winter/releases/tag/v0.1.0
