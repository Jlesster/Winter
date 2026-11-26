#include "term_keys.hpp"

#include <string>

#include "keys.hpp"

namespace {

// xterm modifier parameter: 1 + shift(1) + alt(2) + ctrl(4).
int xmod(int mods) {
    return 1 + ((mods & MOD_SHIFT) ? 1 : 0) + ((mods & MOD_ALT) ? 2 : 0) +
           ((mods & MOD_CTRL) ? 4 : 0);
}

// Arrows, Home, End, F1-F4: ESC [ X / ESC O X plain, ESC [ 1 ; m X modified.
std::string letter_key(char f, int mods, bool ss3) {
    const int m = xmod(mods);
    if(m == 1) return std::string(ss3 ? "\x1bO" : "\x1b[") + f;
    return "\x1b[1;" + std::to_string(m) + f;
}

// Insert, Delete, PageUp/Down, F5-F12: ESC [ n ~ / ESC [ n ; m ~.
std::string tilde_key(int n, int mods) {
    const int m = xmod(mods);
    std::string s = "\x1b[" + std::to_string(n);
    if(m != 1) s += ";" + std::to_string(m);
    return s + '~';
}

// What Ctrl turns a non-letter into. Letters, Space and [ \ ] ^ _ never get
// here: key_normalize() has already folded them into the control byte.
unsigned char ctrl_byte(int base) {
    switch(base) {
        case '2': return 0x00;
        case '3': return 0x1b;
        case '4': return 0x1c;
        case '5': return 0x1d;
        case '6': return 0x1e;
        case '7':
        case '/': return 0x1f;
        case '8':
        case '?': return 0x7f;
        case BACKSPACE:
            return '\x17';  // Ctrl-Alt-Backspace; plain Ctrl is CTRL_BACKSPACE
        default: return static_cast<unsigned char>(base);
    }
}

}  // namespace

std::string term_encode_key(int key, TermKeyMode mode) {
    const int base = key_base(key);
    const int mods = key_mods(key);

    switch(base) {
        case ARROW_UP: return letter_key('A', mods, mode.app_cursor);
        case ARROW_DOWN: return letter_key('B', mods, mode.app_cursor);
        case ARROW_RIGHT: return letter_key('C', mods, mode.app_cursor);
        case ARROW_LEFT: return letter_key('D', mods, mode.app_cursor);
        case HOME: return letter_key('H', mods, mode.app_cursor);
        case END: return letter_key('F', mods, mode.app_cursor);
        case INS: return tilde_key(2, mods);
        case DELETE: return tilde_key(3, mods);
        case PAGE_UP: return tilde_key(5, mods);
        case PAGE_DOWN: return tilde_key(6, mods);
        case CTRL_BACKSPACE: return "\x08";
        case PASTE:
        case NONE: return {};
        default: break;
    }

    if(base >= F1 && base <= F12) {
        static constexpr int kTilde[] = {
            15, 17, 18, 19, 20, 21, 23, 24};  // F5..
        const int n = base - F1;
        if(n < 4) return letter_key(static_cast<char>('P' + n), mods, true);
        return tilde_key(kTilde[n - 4], mods);
    }

    if(base < 0 || base > 0xFF) return {};

    const bool alt = (mods & MOD_ALT) != 0;
    if(base == '\t' && (mods & MOD_SHIFT))  // Shift-Tab: back-tab
        return std::string(alt ? "\x1b" : "") + "\x1b[Z";

    // Plain byte (UTF-8 arrives one byte per key and is passed on the same
    // way). Shift is already baked into the byte; Ctrl on Enter, Tab or Esc has
    // no legacy encoding of its own, so it is dropped.
    unsigned char b = static_cast<unsigned char>(base);
    if(mods & MOD_CTRL) b = ctrl_byte(base);

    std::string s;
    if(alt) s += '\x1b';
    s += static_cast<char>(b);
    return s;
}
