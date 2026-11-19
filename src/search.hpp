#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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
std::string search_last_pattern();  // the last search that found something, or ""
void search_step(int dir, int count = 1);
void search_clear_highlight();
void search_spans(int line, std::string_view text, std::vector<LineSpan>& out);

class Menu;
bool smartcase_sensitive(std::string_view pat, bool regex = false);

struct BufWord {
    std::string text;
    size_t count;
};

std::vector<BufWord> buffer_words();
// Fuzzy menu of buffer words for the word under the cursor in `buf`.
void words_menu_update(Menu& menu, const std::vector<BufWord>& words,
                       const std::string& buf, size_t pos, bool regex);
// Replaces that word with the selected item. False if nothing is selected.
bool words_menu_accept(Menu& menu, std::string& buf, size_t& pos, bool regex);
