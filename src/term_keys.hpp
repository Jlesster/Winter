#pragma once
// Winter key ints (keys.hpp) -> the bytes a program on a PTY expects.
//
// Pure function, no editor state: the panel passes in the two emulator modes
// that change the encoding. An empty result means "nothing to send" (NONE,
// PASTE, a key with no encoding).
#include <string>

struct TermKeyMode {
    bool app_cursor =
        false;  // DECCKM: arrows/Home/End send ESC O x, not ESC [ x
};

std::string term_encode_key(int key, TermKeyMode mode = {});
