#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "buffer.hpp"

struct TreeSeg {
    std::string text;
    Style st;
};

struct TreeLine {
    std::vector<TreeSeg> segs;
    bool selected = false;
    bool visual = false;  // inside the v range (cursor row is `selected`)
};

void tree_toggle();
bool tree_visible();
bool tree_focused();
bool tree_visual();         // a v range is active in the tree
bool tree_visual_cancel();  // ends it; false if there was none
void tree_layout(int total_cols);
int tree_cols();
bool tree_key(int c);
int tree_cursor_row();
const std::vector<TreeLine>& tree_view(int rows);
void tree_move(long d);    // relative, clamped
void tree_goto(long row);  // absolute 0-based row, clamped
