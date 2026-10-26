#pragma once

#include <string>
#include <string_view>
#include <vector>

struct SearchMark {
    size_t lo, hi;
    bool cur;   
};

void editor_find();
void search_step(int dir, int count = 1);
void search_clear_highlight();
void search_marks(int line, std::string_view text, std::vector<SearchMark>& out);
