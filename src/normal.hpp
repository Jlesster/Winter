#pragma once
// Normal mode: counts, chords, and dispatch into the action registry.
#include <string_view>
#include "motion.hpp"
#include "registry.hpp"

void normal_feed(int c);  // feed one key; everything in normal mode goes here
void normal_reset();      // drop pending count / chord

void apply_motion(const Target& t);  // move the cursor (and goal column)
void update_want();                  // refresh the j/k goal column
void move_vertical(int dir, int count);
void editor_move_cursor(int key);  // arrow-key style movement
void normal_begin_operator(std::string_view name, int count);
void run_operator(std::string_view op, std::string_view what, int count,
                  std::string_view arg);
Range visual_range();
void run_operator_on(std::string_view op, const Range& r);
void dot_after_key(int c, EditorMode before, uint64_t rev_before);
void dot_supress();
void dot_replay();
