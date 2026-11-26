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
// Registers every built-in action, motion and default key binding.
// Call once at startup, before the first key is read.

void register_builtins();           // calls the two below
void register_builtin_motions();    // h l w b e gg G f t ; , ...
void register_builtin_actions();    // i a o u / : ...
void register_builtin_operators();  // d c y > < x D p P, text objects
void register_builtin_marks();      // m ' ` and the special marks
