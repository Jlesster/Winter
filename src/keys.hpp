#pragma once

constexpr int MOD_SHIFT = 1 << 16;
constexpr int MOD_ALT = 1 << 17;
constexpr int MOD_CTRL = 1 << 18;
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
    INS,
    CTRL_BACKSPACE,
    PASTE,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    // special action
    DELETE_WORD,
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

// The plain key an *unclaimed* key falls back to. Modifiers are dropped so an
// unbound <A-j> behaves like j, <A-CR> like Enter, <A-Left> like Left, and
// <A-S-j> like J. Ctrl-Space / Ctrl-@ normalize to NUL and have no plain-key
// equivalent. NONE means "pass through as nothing".
inline int key_passthrough(int k) {
    const int base = key_base(k);
    return base == 0 ? NONE : base;
}

// the layer under the keymaps for insert/replace. A modified key nobody bonund
// become sthe editing action people expect from any other editor; every other
// modified key falls back to the plain kay exactly like key_passthrough()
inline int key_basic(int k) {
    const int base = key_base(k);
    switch(key_mods(k)) {
        case MOD_CTRL:
            if(base == DELETE) return DELETE_WORD;
            break;
        case MOD_ALT:
            if(base == BACKSPACE) return CTRL_BACKSPACE;
            if(base == 'd') return DELETE_WORD;
            break;
    }
    return key_passthrough(k);
}
