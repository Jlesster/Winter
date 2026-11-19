#pragma once

// Total terminal size; the editor reserves the last two rows for the
// status and message bars.
void render_resize(int rows, int cols);
void editor_refresh_screen();
int editor_message_row();  // the row of the message bar / command prompt
int editor_screen_cols();  // the whole terminal, not the focused window
