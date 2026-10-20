#pragma once
#include "core.hpp"

struct TermCaps {
    bool scroll_region = false;
    bool cursor_shapes = false;
};
const TermCaps& term_caps();

[[noreturn]] void die(const char* s);
void editor_quit();
void enable_raw_mode();
void disable_raw_mode();
int editor_read_key();

int get_window_size(int* rows, int* cols);
int query_window_size(int* rows, int* cols);

void install_winch_handler();
bool resize_pending();
void resize_ack();

void set_cursor_for_mode(EditorMode m);
