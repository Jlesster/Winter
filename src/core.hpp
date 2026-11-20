#pragma once

#define WINTER_VERSION "0.1.2"
#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorMode {
    INSERT,
    TERMINAL,
    REPLACE,
    SEARCH,
    NORMAL,
    VISUAL,
    COMMAND,
    FILES,
    OPPEND
};

inline bool insertish(EditorMode m) { return m == INSERT || m == REPLACE; }
