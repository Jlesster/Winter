#pragma once

#define _BSD_SOURCE
#define WINTER_VERSION "0.0.1"

#define WINTER_EXPAND_TAB 1
#define WINTER_TAB_STOP 4
#define WINTER_SHIFT_WIDTH 4

#define WINTER_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorMode { INSERT, SEARCH, NORMAL, VISUAL, COMMAND };
