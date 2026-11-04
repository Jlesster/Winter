#pragma once
#include <cstddef>
#include <string_view>
#include "piece.hpp"

struct Caret {
    size_t line = 0, col = 0;  // col in bytes
};

// Make `pt` hold `text` (file bytes: one trailing '\n' is folded into `eol`).
// Applied as minimal LINE hunks inside one undo group, bottom-up, so
// tree-sitter/LSP/decos/anchors see small edits and the caret stays put.
// `caret` (optional) is mapped through the diff. True if the text changed.
bool pt_replace(PieceTable& pt, bool& eol, std::string_view text, Origin o,
                Caret* caret = nullptr);
