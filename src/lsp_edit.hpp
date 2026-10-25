#pragma once
#include <optional>
#include "json.hpp"
#include "piece.hpp"

std::optional<Json> lsp_range_change(const PieceTable& pt, const TextEdit& e,
                                     bool utf16);
bool lsp_utf8_valid(std::string_view s);
