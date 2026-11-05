#pragma once

enum : int { MOD_SHIFT = 1 << 16, MOD_ALT = 1 << 17, MOD_CTRL = 1 << 18 };
constexpr int KEY_MODS = MOD_SHIFT | MOD_ALT | MOD_CTRL;
constexpr int key_base(int k) { return k & ~KEY_MODS; }
constexpr int key_mods(int k) { return k & KEY_MODS; }

enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    DELETE,
    HOME,
    END,
    CTRL_BACKSPACE,
    PASTE,
    NONE,
};

inline int key_normalize(int base, int mods) {
    const bool lower = base >= 'a' && base <= 'z';
    const bool upper = base >= 'A' && base <= 'Z';

    if(base == BACKSPACE && mods == MOD_CTRL) return CTRL_BACKSPACE;

    if(mods & MOD_CTRL) {
        if((lower || upper) && (mods & MOD_SHIFT)) return -1;
        if(lower || upper || base == ' ' || base == '@' ||
           (base >= '[' && base <= '_'))
            return (base & 0x1f) | (mods & MOD_ALT);  // the control byte
        return base | mods;                           // Ctrl-Left, Ctrl-1 ...
    }
    if(mods & MOD_SHIFT) {
        if(lower) return (base - 32) | (mods & MOD_ALT);
        if(upper) return base | (mods & MOD_ALT);
        if(base > ' ' && base < 127) return -1;
    }
    return base | mods;
}

// What an *unclaimed* key should type. NONE means "not typable".
// Modified space/Enter stay distinct in key_normalize so they can be mapped;
// only at fallback time does S-Space become a space and modified Enter become
// a plain Enter.
inline int key_text(int k) {
    const int base = key_base(k);
    const int mods = key_mods(k);

    if(base >= ARROW_LEFT) return base;  // Ctrl-Left/Shift-Home still navigate
    if(base == 0) return NONE;           // Ctrl-Space / Ctrl-@ carry no text
    if(mods == 0) return k;              // ordinary text, control bytes, Tab...
    if(base == '\r') return '\r';        // <S-CR>/<C-CR>/<A-CR> act like Enter
    if((mods & ~MOD_SHIFT) == 0 && base == ' ') return ' ';
    return NONE;  // Alt-/Ctrl-only combos type nothing
}

