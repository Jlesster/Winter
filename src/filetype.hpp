#pragma once
#include <string>
#include <string_view>

// File type -> tree-sitter language. One resolver for every caller, layered so
// no single signal (the extension) has to be reliable:
//   1. exact file name        (Makefile, .bashrc, tmux.conf, PKGBUILD, ...)
//   2. name prefix            (Dockerfile.dev)
//   3. extension, longest first (.d.ts before .ts), then installed-grammar
//      metadata, then data synced from GitHub linguist
//   4. shebang of the first line (#!/usr/bin/env -S bash)
// Names resolve through aliases (jsonc -> json) and fall back to a related
// installed grammar (zsh -> bash) so a missing parser never costs highlighting
// when a close one is available.
namespace ft {

// Language for a file, or {}. `first_line` may be empty. The view stays valid
// for the life of the process.
std::string_view detect(std::string_view path, std::string_view first_line = {});

// Canonical language for a user/markdown-fence name ("sh", "c++", "jsonc").
std::string normalize(std::string name);

// Rebuild <data>/filetypes.tsv from <data>/languages.yml (linguist). Call after
// the grammar table was refreshed. False + err when the source is unusable.
bool rebuild(std::string& err);

// Drop cached tables (re-read on next use).
void invalidate();

}  // namespace ft
