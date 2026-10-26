#include "input.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

#include "command.hpp"
#include "core.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "search.hpp"
#include "terminal.hpp"
#include "utf8.hpp"

static int quit_presses = 0;

void editor_handle_key(int c) {
    if(c == PASTE) {
        const EditorMode before = E.mode;
        const uint64_t rev = E.buf().pt.revision();
        normal_reset();
        editor_paste(editor_take_paste());
        dot_supress();
        dot_after_key(c, before, rev);
        return;
    }
    const EditorMode before = E.mode;
    const uint64_t rev_before = E.buf().pt.revision();

    switch(c) {
        case CTRL_KEY('q'):
            if(E.buf().pt.modified() && quit_presses < opts.quit_times) {
                editor_set_status_message(
                    "[WARNING]!! File has unsaved changes, press CTRL-Q {} "
                    "more times to quit.",
                    opts.quit_times - quit_presses);
                quit_presses++;
                return;
            }
            editor_quit();
            break;

        case CTRL_KEY('s'): editor_save(); break;

        case HOME: E.cx = 0; break;
        case END:
            if(E.cy < E.num_rows()) E.cx = static_cast<int>(line_len(E.cy));
            break;

        case PAGE_UP:
        case PAGE_DOWN: {
            if(c == PAGE_UP || c == 'g') {
                E.cy = E.row_offset;
            } else if(c == PAGE_DOWN || c == 'G') {
                E.cy = E.row_offset + E.term_rows - 1;
                if(E.cy > E.num_rows() - 1) E.cy = E.num_rows() - 1;
            }
            int times = E.term_rows;
            while(times--) {
                editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
        } break;

        case ARROW_UP: editor_move_cursor(c); break;
        case ARROW_DOWN: editor_move_cursor(c); break;
        case ARROW_LEFT: editor_move_cursor(c); break;
        case ARROW_RIGHT: editor_move_cursor(c); break;

        case CTRL_KEY('l'):
        case '\x1b': {
            const bool was_insert = E.mode == EditorMode::INSERT;
            set_mode(EditorMode::NORMAL);
            if(was_insert && E.cx > 0)
                E.cx = static_cast<int>(
                    utf8_prev(E.buf().pt.line_text(E.cy), E.cx));
            update_want();
        } break;
    }
    switch(E.mode) {
        case INSERT:
            switch(c) {
                case '\r': editor_insert_newline(); break;
                case BACKSPACE: editor_delete_char(); break;
                case CTRL_BACKSPACE:
                case CTRL_KEY('h'):
                case CTRL_KEY('w'): editor_delete_word_back(); break;
                case DELETE: editor_delete_forward(); break;
                case ARROW_UP:
                case ARROW_DOWN:
                case ARROW_LEFT:
                case ARROW_RIGHT:
                case HOME:
                case END:
                case PAGE_UP:
                case PAGE_DOWN:
                    E.buf().pt.end_group();
                    E.buf().pt.begin_group();
                    break;
                default:
                    if(c >= 32 && c < 127) editor_type_char(c);
                    else if(c == '\t') editor_insert_tab();
                    else if(c >= 0x80 && c < 0x100) editor_insert_char(c);
                    break;
            }
            break;
        case NORMAL:
            normal_feed(c);  // keys -> registry -> action: see builtin_*.cpp
            break;
        case VISUAL: normal_feed(c); break;
        case COMMAND: break;
        case SEARCH: break;
        case OPPEND: break;
    }
    quit_presses = 0;
    dot_after_key(c, before, rev_before);
}

void editor_process_key() {
    if(E.mode == EditorMode::COMMAND) {
        editor_command_prompt();
        return;
    } else if(E.mode == EditorMode::SEARCH) {
        editor_find();
        return;
    }
    using clock = std::chrono::steady_clock;
    static std::optional<clock::time_point> deadline;
    if(opts.timeoutlen > 0 && normal_chord_pending()) {
        if(!deadline)
            deadline =
                clock::now() + std::chrono::milliseconds(opts.timeoutlen);
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              *deadline - clock::now())
                              .count();
        if(left <= 0) {
            deadline.reset();
            normal_chord_timeout();
            return;
        }
        if(!loop_wait(static_cast<int>(left))) return;
    }
    deadline.reset();
    const int c = editor_read_key();
    if(c == NONE) return;
    editor_handle_key(c);
}
