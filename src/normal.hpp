#pragma once
// Normal mode: counts, chords and motions.

// Feed one key. Returns true if it was consumed; false means "not a chord,
// let the caller handle it" (any typed count stays readable by the caller).
bool normal_feed(int c);
void normal_reset();  // drop pending count / chord

void update_want();  // refresh the j/k goal column from the cursor
void move_vertical(int dir, int count);
void editor_move_cursor(int key);  // arrow-key style movement
