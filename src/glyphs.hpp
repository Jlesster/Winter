#pragma once
#include <string>
#include <string_view>
#include "options.hpp"

struct Icons {
    std::string_view dir, dir_open, file;
};

struct BorderSet {
    std::string_view lt, t, tr, l, r, bl, b, br;
};

const Icons& icons();
const BorderSet& border_set(Border);
std::string_view file_icon(const std::string& ext);
std::optional<uint32_t> file_icon_color(const std::string& ext);
void tree_icon_set(std::string_view ext, std::string_view glyph);
