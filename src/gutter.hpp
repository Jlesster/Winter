#pragma once
#include <string>

int gutter_width(int line_count);
std::string gutter_label(int line, int cursor_line, int width);
