// Text edits at the cursor: typing, auto-indent, auto-pairs, line opening.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

void editor_insert_char(int c);
void editor_insert_tab();
void editor_insert_newline();
void editor_type_char(int c);  // printable ascii in insert mode (auto-pairs)
void editor_open_below();      // o
void editor_open_above();      // O
void editor_strip_autoindent();
// S / cc: the cursor line was just emptied; give it its computed indent
// (tree-sitter, same as o/O), or `fallback` when no indent rule applies.
// Leaving insert without typing strips it again, like o/O.
void editor_indent_blank_line(std::string_view fallback);
void editor_delete_char();                                    // backspace
void editor_delete_forward();                                 // delete
void editor_delete_word_back();                               // ctrl+backspace
void editor_delete_word_forward();                            // ctrl+delete
void editor_kill_to_bol();                                    // ctrl+u
void editor_kill_to_eol();                                    // ctrl+k
void editor_paste(std::string text);                          // bracketed paste
void replace_char_at_cursor(int count, std::string_view ch);  // normal mode r
bool bump_number_at_cursor(int64_t delta, bool int_only);     // increment num
bool bump_digit_at_cursor(
    int64_t delta);  // increment num directly under cursor
void bump_numbers_in_range(size_t start,
                           size_t end,
                           bool linewise,
                           int64_t delta,
                           bool progressive);  // visual increment num

void toggle_case_at_cursor(int count);  // normal mode case swap
void toggle_case_in_range(size_t start,
                          size_t end,
                          bool linewise);  // visual mode case swap

// joins following lines into E.cy (current line/cursor line)
void join_lines(size_t first, size_t joins);

void replace_reset();     // forget R's undo state
bool replace_key(int c);  // true if key handled within R/r
size_t word_back_stop(std::string_view line, size_t pos);
size_t word_fwd_stop(std::string_view line, size_t pos);
