#pragma once

// Total terminal size; the editor reserves the last two rows for the
// status and message bars.
void render_resize(int rows, int cols);
void editor_refresh_screen();
