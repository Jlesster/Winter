/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
// Normal mode: counts, chords, and dispatch into the action registry.
#include <string_view>
#include "motion.hpp"
#include "registry.hpp"

void normal_feed(int c);  // feed one key; everything in normal mode goes here
bool normal_accepts(int c);   // would c continue or finish a mapping?
void normal_reset();          // drop pending count / chord
bool normal_chord_pending();  // chord waiting
bool normal_input_pending();  // input waiting
void normal_chord_timeout();  // resolve or timeout waiting chord

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
// Keys fed while this is on are not remembered for `.` (dot-repeat itself and
// injected keys use it). Returns the previous value.
bool dot_set_replaying(bool on);
bool dot_replaying();
