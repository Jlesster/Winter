#pragma once
// Raw mode, key input, terminal size, SIGWINCH and cursor shape.

#include <cstdint>
#include "core.hpp"

struct TermCaps {
    bool scroll_region = false;  // DECSTBM scroll optimisation
    bool cursor_shapes =
        false;  // DECSCUSR (neither exists on the linux console)
};
const TermCaps& term_caps();  // valid after enable_raw_mode()

[[noreturn]] void die(const char* s);
void editor_quit();
void enable_raw_mode();
void disable_raw_mode();
int editor_read_key();  // blocks; returns an EditorKey (keys.hpp) or a byte

int get_window_size(int* rows, int* cols);
int query_window_size(int* rows, int* cols);  // ioctl only, never blocks

void install_winch_handler();
bool resize_pending();
void resize_ack();

void set_cursor_color(uint32_t rgb);
void set_cursor_for_mode(EditorMode m);  // no-op without DECSCUSR
