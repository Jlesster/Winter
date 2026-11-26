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
// A shell on a PTY, shown in the dock (or the float). The panel owns the
// process (pty.hpp) and the emulator (vt.hpp); the host owns the panel.
//
// Keys: every key goes to the child (raw_keys). <C-\><C-n> leaves the panel
// (focus back to the editor), <C-\><C-\> sends a literal Ctrl-\.
#include <string>
#include <string_view>
#include <vector>

#include "dock.hpp"

// Creates a terminal panel. The shell starts when the panel is first drawn, so
// it is born at the real size. argv empty = $SHELL.
PanelId term_open(std::string cwd = {}, std::vector<std::string> argv = {});

void term_register_commands();  // :Term
void term_run(std::string_view sub);
void term_toggle(std::string_view pos = {});
