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
