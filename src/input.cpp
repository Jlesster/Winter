#include "input.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

#include "chord.hpp"
#include "command.hpp"
#include "complete.hpp"
#include "core.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "overlay.hpp"
#include "search.hpp"
#include "snippet.hpp"
#include "terminal.hpp"
#include "tree.hpp"
#include "utf8.hpp"

namespace {

Chord g_insert_chord;

// The old INSERT/REPLACE body, extracted so chord replay uses the same path.
void insert_literal(int c) {
    if(c == '\t') {
        if(snippet_active()) {
            snippet_next();
            return;
        }
        if(snippet_try_trigger()) return;
    }
    if(snippet_pre_key(c)) return;
    if(E.mode == REPLACE && replace_key(c)) return;

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
}

// Replay a pending chord key as text, with the normal insert-mode hooks.
void insert_replay(int key) {
    const int c = key_text(key);
    if(c == NONE) return;

    const EditorMode before = E.mode;
    const uint64_t rev = E.buf().pt.revision();
    insert_literal(c);
    snippet_post_key();
    completion_post_key(c);
    dot_after_key(c, before, rev);
}

}  // namespace

void editor_handle_key(int c) {
    if(c == PASTE) {
        g_insert_chord.reset();
        if(overlay_modal()) {
            editor_take_paste();
            return;
        }
        const EditorMode before = E.mode;
        const uint64_t rev = E.buf().pt.revision();
        normal_reset();
        editor_paste(editor_take_paste());
        dot_supress();
        dot_after_key(c, before, rev);
        return;
    }

    // Text-entry mappings run before the modifier gate, so <S-Space> and
    // <C-Space> can start a chord.
    if(!overlay_modal() && insertish(E.mode)) {
        if(g_insert_chord.feed(E.mode, c, insert_replay) != Chord::Result::Pass)
            return;
    }

    if(key_mods(c)) {
        const bool keymapped =
            !overlay_modal() &&
            (E.mode == NORMAL || E.mode == VISUAL || E.mode == FILES);
        if(!(keymapped && normal_accepts(c))) {
            if(insertish(E.mode)) {
                c = key_text(c);
                if(c == NONE) return;
            } else {
                c = key_base(c) >= ARROW_LEFT ? key_base(c) : NONE;
                if(c == NONE) return;
            }
        }
    }
    if(c != PASTE && overlay_key(c)) return;
    const EditorMode before = E.mode;
    const uint64_t rev_before = E.buf().pt.revision();
    if(E.mode == FILES && (c == '\x1b' || c == CTRL_KEY('l')) &&
       tree_visual_cancel())
        return;  // Esc in a tree range: drop the range, stay in the tree

    switch(c) {
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
            snippet_end();
            const bool was_insert = insertish(E.mode);
            set_mode(EditorMode::NORMAL);
            if(was_insert && E.cx > 0)
                E.cx = static_cast<int>(
                    utf8_prev(E.buf().pt.line_text(E.cy), E.cx));
            update_want();
        } break;
    }
    switch(E.mode) {
        case INSERT:
        case REPLACE:
            insert_literal(c);
            break;
        case NORMAL:
            normal_feed(c);  // keys -> registry -> action: see builtin_*.cpp
            break;
        case VISUAL: normal_feed(c); break;
        case FILES:
            if(normal_input_pending() || !tree_key(c)) normal_feed(c);
            break;
        case COMMAND: break;
        case SEARCH: break;
        case OPPEND: break;
    }
    snippet_post_key();
    completion_post_key(c);
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
    const bool chord_pending =
        normal_chord_pending() || g_insert_chord.pending();
    if(opts.timeoutlen > 0 && chord_pending) {
        if(!deadline)
            deadline =
                clock::now() + std::chrono::milliseconds(opts.timeoutlen);
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              *deadline - clock::now())
                              .count();
        if(left <= 0) {
            deadline.reset();
            if(normal_chord_pending()) normal_chord_timeout();
            else g_insert_chord.timeout(insert_replay);
            return;
        }
        if(!loop_wait(static_cast<int>(left))) return;
    }
    deadline.reset();
    const int c = editor_read_key(true);
    if(c == NONE) return;
    editor_handle_key(c);
}
