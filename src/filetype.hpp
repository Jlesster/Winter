/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <string>
#include <string_view>

// File type -> tree-sitter language. One resolver for every caller, layered so
// no single signal (the extension) has to be reliable:
//   1. exact file name        (Makefile, .bashrc, tmux.conf, PKGBUILD, ...)
//   2. name prefix            (Dockerfile.dev)
//   3. extension, longest first (.d.ts before .ts), then installed-grammar
//      metadata
//   4. directory   (~/.config/zsh/aliases, tmux/keys.conf, fish/functions/x);
//      nearest directory wins, only for files with no extension or a generic
//      one (.conf .local .inc .bak)
//   5. data synced from GitHub linguist
//   6. content: a vim modeline, the shebang (#!/usr/bin/env -S bash) and a few
//      shell/tmux-only constructs
// Every name rule also sees the file name with a /nix/store/<hash>- prefix and
// a trailing .local/.bak/... removed, and a symlink is followed when the link
// itself matched nothing, so home-manager and stow managed dotfiles resolve.
// Names resolve through aliases (jsonc -> json) and fall back to a related
// installed grammar (zsh -> bash) so a missing parser never costs highlighting
// when a close one is available.
namespace ft {

// Language for a file, or {}. `head` is the start of the file (a few hundred
// bytes is plenty) and may be empty. The view stays valid for the life of the
// process.
std::string_view detect(std::string_view path, std::string_view head = {});

// What a file wants versus what can be attached right now. `want` is the
// language the file is (zsh), `use` the installed grammar that will highlight
// it (zsh, or bash while zsh is missing); `use` is empty when none is usable
// yet, in which case the caller may install `want`.
struct Resolved {
    std::string_view want, use;
};
Resolved resolve(std::string_view path, std::string_view head = {});

// One line saying what was decided and by which rule, for :ft, e.g.
// "tmux via directory 'tmux'; parser: tmux".
std::string explain(std::string_view path, std::string_view head = {});

// Canonical language for a user/markdown-fence name ("sh", "c++", "jsonc").
std::string normalize(std::string name);

// Rebuild <data>/filetypes.tsv from <data>/languages.yml (linguist). Call after
// the grammar table was refreshed. False + err when the source is unusable.
bool rebuild(std::string& err);

// Drop cached tables (re-read on next use).
void invalidate();

}  // namespace ft
