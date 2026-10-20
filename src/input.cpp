#include "input.hpp"

#include <string>

#include "command.hpp"
#include "core.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "keys.hpp"
#include "normal.hpp"
#include "search.hpp"
#include "terminal.hpp"
#include "utf8.hpp"

namespace {

static void enter_insert_at(int cx) {
    E.cx = cx;
    set_mode(EditorMode::INSERT);
}

}  // namespace

void editor_process_key() {
    static int quit_times = WINTER_QUIT_TIMES;
    if(E.mode == EditorMode::COMMAND) {
        editor_command_prompt();
        return;
    } else if(E.mode == EditorMode::SEARCH) {
        editor_find();
        return;
    }
    int c = editor_read_key();
    if(c == NONE) return;
    switch(c) {
        case CTRL_KEY('q'):
            if(E.pt.modified() && quit_times > 0) {
                editor_set_status_message(
                    "[WARNING]!! File has unsaved changes, press CTRL-Q {} "
                    "more times to quit.",
                    quit_times);
                quit_times--;
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
                E.cx = static_cast<int>(utf8_prev(E.pt.line_text(E.cy), E.cx));
            update_want();
        } break;
    }
    switch(E.mode) {
        case INSERT:
            switch(c) {
                case '\r': editor_insert_newline(); break;
                case BACKSPACE: editor_delete_char(); break;
                case DELETE: editor_delete_forward(); break;
                case ARROW_UP:
                case ARROW_DOWN:
                case ARROW_LEFT:
                case ARROW_RIGHT:
                case HOME:
                case END:
                case PAGE_UP:
                case PAGE_DOWN:
                    E.pt.end_group();
                    E.pt.begin_group();
                    break;
                default:
                    if(c >= 32 && c < 127) editor_type_char(c);
                    else if(c == '\t') editor_insert_tab();
                    else if(c >= 0x80 && c < 0x100) editor_insert_char(c);
                    break;
            }
            break;
        case NORMAL:
            if(normal_feed(c)) break;
            switch(c) {
                case 'n':
                case 'N': editor_find_callback("", c); break;

                case '/': set_mode(EditorMode::SEARCH); break;
                case 'v': set_mode(EditorMode::VISUAL); break;
                case 'i': set_mode(EditorMode::INSERT); break;

                case 'a': {  // insert *after* the char under the cursor
                    const std::string line = E.pt.line_text(E.cy);
                    const int size = static_cast<int>(line.size());
                    enter_insert_at(
                        E.cx < size ? static_cast<int>(utf8_next(line, E.cx))
                                    : E.cx);  // empty line: stay put
                } break;

                case 'A':  // append at the very end of the line
                    enter_insert_at(static_cast<int>(line_len(E.cy)));
                    break;

                case 'I': {  // insert at the first non-blank
                    const std::string line = E.pt.line_text(E.cy);
                    size_t i = 0;
                    while(i < line.size() &&
                          (line[i] == ' ' || line[i] == '\t'))
                        ++i;
                    enter_insert_at(static_cast<int>(i));
                } break;

                case 'o':
                    set_mode(EditorMode::INSERT);  // first: the new line joins
                    editor_open_below();           // the insert's undo group
                    break;
                case 'O':
                    set_mode(EditorMode::INSERT);
                    editor_open_above();
                    break;
                case ':': set_mode(EditorMode::COMMAND); break;

                case 'u':
                    if(auto at = E.pt.undo()) set_cursor_offset(*at);
                    else editor_set_status_message("Already at oldest change");
                    break;

                case CTRL_KEY('r'):
                    if(auto at = E.pt.redo()) set_cursor_offset(*at);
                    else editor_set_status_message("Already at newest change");
                    break;
            }
            normal_reset();
            break;
        case VISUAL: break;
        case COMMAND: break;
        case SEARCH: break;
    }
    quit_times = WINTER_QUIT_TIMES;
}
