#pragma once

#include <string_view>
#include <vector>
#include "virt.hpp"

inline constexpr uint32_t kSearchBg = 0x585b70;     // every match
inline constexpr uint32_t kSearchCurBg = 0xf9e2af;  // the one under the cursor
inline constexpr uint32_t kSearchCurFg = 0x1e1e2e;
inline constexpr uint32_t kSearchCurCursor =
    0x89b4fa;  // cursor block over query
inline constexpr int kPrioSearch = 10;

void editor_find();
void search_step(int dir, int count = 1);
void search_clear_highlight();
void search_spans(int line, std::string_view text, std::vector<LineSpan>& out);
