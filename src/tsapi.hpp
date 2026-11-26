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
// What winter.ts needs from whoever hosts it. The editor installs a provider
// backed by Buffer/Syntax (ts_provider.cpp); the tests install one over a bare
// parser. Keeps the Lua binding free of editor dependencies.
#include <tree_sitter/api.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "tsq.hpp"

struct TsSnapshot {
    uint64_t buf = 0;               // the resolved buffer id
    const TSTree* tree = nullptr;   // null: the buffer has no syntax tree
    uint64_t gen = 0;               // changes whenever `tree` is replaced or edited
    std::string lang;
    TsText text;                    // byte range -> source text
};

struct TsProvider {
    // buf 0 = the current buffer. Brings the tree up to date with the text.
    // False when there is no such buffer.
    std::function<bool(uint64_t buf, TsSnapshot& out)> snapshot;
    std::function<const TSLanguage*(std::string_view name)> language;
};

void ts_set_provider(TsProvider p);
