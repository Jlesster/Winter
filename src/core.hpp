#pragma once

#define WINTER_VERSION "0.0.1"
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)
#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorMode { INSERT, SEARCH, NORMAL, VISUAL, COMMAND };
