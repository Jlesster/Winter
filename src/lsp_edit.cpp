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

#include "lsp_edit.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include "utf8.hpp"

namespace {
bool utf8_valid(std::string_view s) {
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        if(d.len == 1 && d.cp == 0xFFFD &&
           static_cast<unsigned char>(s[i]) >= 0x80)
            return false;
        i += d.len;
    }
    return true;
}

Json position(const LineCol& p, size_t u16, bool utf16) {
    return Json::obj({{"line", p.line}, {"character", utf16 ? u16 : p.col}});
}
}  // namespace

bool lsp_utf8_valid(std::string_view s) { return utf8_valid(s); }

std::optional<Json> lsp_range_change(const PieceTable& pt, const TextEdit& e,
                                     bool utf16) {
    if(e.reset) return std::nullopt;
    std::string text = pt.text(e.start, e.new_end - e.start);
    if(!utf8_valid(text)) return std::nullopt;
    Json change;
    change.set(
        "range",
        Json::obj({{"start", position(e.start_pt, e.start_u16, utf16)},
                   {"end", position(e.old_end_pt, e.old_end_u16, utf16)}}));
    change.set("text", std::move(text));
    return change;
}
