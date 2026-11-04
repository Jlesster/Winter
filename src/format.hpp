#pragma once
#include <string>

struct Buffer;

void format_init();  // after script_init(), before script_load_config()

// Run the plan for this buffer now. verbose = report in the status line.
// true if the text changed.
bool format_buffer(Buffer& b, bool verbose);

// Save path: never blocks a save; failures are returned as a note.
void format_on_save(Buffer& b);
std::string format_take_note();  // "" or ", format failed: ..." (clears it)
