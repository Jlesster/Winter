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
#include <vector>
// input exports, not much but honest work uwu

void editor_process_key();
void editor_handle_key(int c);

// Feed keys (the ints parse_keys makes) from a script.
//  editor_inject_keys: right now, in the current mode, like :normal. Not
//    remembered for `.`. With `complete`, whatever the keys left half done (an
//    operator, a chord, insert mode, a command line they started) is ended as if
//    <Esc> was typed. False when injections nest too deep.
//  editor_feed_keys: on the next turn of the event loop, exactly as if typed
//    (remembered for `.`, may leave a command pending).
bool editor_inject_keys(const std::vector<int>& keys, bool complete);
void editor_feed_keys(std::vector<int> keys);
