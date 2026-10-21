#pragma once
// Normal mode: counts, chords, and dispatch into the action registry.

#include "motion.hpp"

void normal_feed(int c);  // feed one key; everything in normal mode goes here
void normal_reset();      // drop pending count / chord

void apply_motion(const Target& t);  // move the cursor (and goal column)
void update_want();                  // refresh the j/k goal column
void move_vertical(int dir, int count);
void editor_move_cursor(int key);    // arrow-key style movement
