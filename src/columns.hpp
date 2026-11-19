#pragma once

#include <cstddef>
#include <string_view>

bool is_ctl(char32_t cp);
int cell_width(char32_t cp, int rx);        // rx = column the char starts at
// cells of the grapheme cluster line[at, end) drawn at column rx (tab / control aware)
int cluster_width(std::string_view line, size_t at, size_t end, int rx);
int snap_cx(std::string_view line, int cx); // snap a byte offset to a char start
int cx_to_rx(std::string_view line, int cx);
int rx_to_cx(std::string_view line, int rx);
int text_width(std::string_view s);
