#pragma once
#include <cstddef>
#include <string>

// Ctrl-o / ctrl-i style position history, shared by every "go somewhere else"
// feature (LSP goto, pickers, quickfix).
void jump_push();            // record the current spot; call BEFORE moving away
void jump_older(int count);  // <C-o>
void jump_newer(int count);  // <C-i>

// '{x} (exact = false: the mark's line) and `{x} (its exact spot). Records the
// current spot first. False, with a message, when the mark is not set.
bool jump_to_mark(char name, bool exact);

// buffer_open + verify that `path` really is the current buffer afterwards.
bool jump_open(const std::string& path);
// open `path` and put the cursor at (0-based line, byte column). No history.
bool jump_goto(const std::string& path, size_t line, size_t col);
