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
