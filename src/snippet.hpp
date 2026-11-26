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
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct LineSpan;

void snippet_init();
bool snippet_expand(std::string_view body);
// Multi cursor: the primary cursor expands the snippet (snippet_expand), every
// other one inserts it with snippet_peer, which only remembers where its tab
// stops are. After the pass, snippet_place_peers puts those cursors on the
// first stop; snippet_next moves them along with the primary.
void snippet_peer(std::string_view body);
void snippet_place_peers();
bool snippet_active();
void snippet_next();
bool snippet_pre_key(int c);
void snippet_post_key();
void snippet_end();
void snippet_add(std::string trigger, std::string body);
void snippet_clear();
bool snippet_try_trigger();
void snippet_spans(size_t ls, size_t len, std::vector<LineSpan>& out);
