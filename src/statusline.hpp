#pragma once
// The statusbar is a row of components. A component is a function that
// returns a StatusBlock (or nothing, to hide itself); the layout names which
// components go on the left and right. Built-ins and Lua components share
// this registry, so `winter.statusline` can reorder, replace or extend any of
// them.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "buffer.hpp"

struct StatusPiece {
    std::string text;
    Style st{};  // patched over the block's style
};

struct StatusBlock {
    std::vector<StatusPiece> pieces;  // empty = hidden
    Style base{};                     // block colours; bg decides separators
    bool pad = true;                  // one space on each side
    int prio = 50;                    // lowest goes first on narrow terminals
};

using StatusFn = std::function<StatusBlock()>;

struct StatusLayout {
    std::vector<std::string> left, right;
    // Arrow glyphs between blocks of different background. nullopt = auto
    // (powerline arrows with nerdicons, nothing without); "" = none.
    std::optional<std::string> sep_left, sep_right;
    bool enabled = true;
};

struct StatusItem {
    std::string name;
    StatusBlock block;
};

void statusline_add(std::string name, StatusFn fn);  // add or replace
bool statusline_remove(std::string_view name);
void statusline_reset();
bool statusline_has(std::string_view name);
std::vector<std::string> statusline_names();

StatusLayout& statusline_layout();
void statusline_reset_layout();

// The blocks for one side, in order; hidden components are dropped.
std::vector<StatusItem> statusline_eval(const std::vector<std::string>& names);

Style statusline_bar_style();           // ui.statusline
std::string statusline_sep(bool left);  // resolved glyph, "" = none
