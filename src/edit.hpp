#pragma once
// Text edits at the cursor: typing, auto-indent, auto-pairs, line opening.

void editor_insert_char(int c);
void editor_insert_tab();
void editor_insert_newline();
void editor_type_char(int c);  // printable ascii in insert mode (auto-pairs)
void editor_open_below();      // o
void editor_open_above();      // O
void editor_delete_char();     // backspace
void editor_delete_forward();  // delete
