#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include "piece.hpp"
#include "syntax.hpp"

namespace indent {
std::string unit();
std::string_view leading_ws(std::string_view line);
std::string ws_for_cols(int cols);
std::string for_newline(std::string_view before);
std::string for_open_above(std::string_view line);
void reindent_lines(Syntax& syn, PieceTable& pt, bool eol, size_t l0,
                    size_t l1);
std::optional<std::string> from_tree(Syntax& syn, PieceTable& pt, bool eol,
                                     size_t line);
}  // namespace indent
