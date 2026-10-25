// Text edits at the cursor: typing, auto-indent, auto-pairs, line opening.
#pragma once
#include <cstddef>
#include <string_view>

void editor_insert_char(int c);
void editor_insert_tab();
void editor_insert_newline();
void editor_type_char(int c);    // printable ascii in insert mode (auto-pairs)
void editor_open_below();        // o
void editor_open_above();        // O
void editor_delete_char();       // backspace
void editor_delete_forward();    // delete
void editor_delete_word_back();  // ctrl+backspace
size_t word_back_stop(std::string_view line, size_t pos);
