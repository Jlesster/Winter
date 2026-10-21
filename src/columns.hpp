#pragma once
// Pure text-geometry helpers: bytes <-> display columns. No editor state.

#include <string_view>

bool is_ctl(char32_t cp);
int cell_width(char32_t cp, int rx);        // rx = column the char starts at
int snap_cx(std::string_view line, int cx); // snap a byte offset to a char start
int cx_to_rx(std::string_view line, int cx);
int rx_to_cx(std::string_view line, int rx);
int text_width(std::string_view s);
