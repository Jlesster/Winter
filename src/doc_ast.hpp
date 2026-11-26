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
// :Doc cursor detection from the syntax tree. Editor-independent on purpose
// (a tree, a text callback, a language name), so it can be tested on its own.
// doc.cpp owns the lookup; this only says WHAT to look up.
#include <tree_sitter/api.h>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace doc {

struct Cand {
    std::string name;  // '.'-separated; doc.cpp rewrites it to the docset's sep
    bool raw = false;  // look up byte for byte (header names like "stdio.h")
};

struct Query {
    std::vector<Cand> cands;  // tried in order, first hit wins
    std::string member;       // member access: last segment, for narrowing
    std::string seed;         // picker seed on a total miss
};

using TextFn = std::function<std::string(size_t, size_t)>;

// The query for the name under byte offset `byte`; nullopt when the tree says
// nothing useful there (no row for the language, comment/string, punctuation),
// so the caller falls back to the plain text token.
std::optional<Query> query_at(const TSTree* tree,
                              std::string_view lang,
                              const TextFn& text,
                              size_t byte);

}  // namespace doc
