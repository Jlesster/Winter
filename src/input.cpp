#include "input.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "chord.hpp"
#include "columns.hpp"
#include "command.hpp"
#include "complete.hpp"
#include "core.hpp"
#include "dock.hpp"
#include "events.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "grapheme.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "motion.hpp"
#include "multicursor.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "overlay.hpp"
#include "reg.hpp"
#include "registry.hpp"
#include "search.hpp"
#include "snippet.hpp"
#include "terminal.hpp"
#include "tree.hpp"

namespace {

Chord g_insert_chord;
bool g_wait_reg = false;  // <C-r> typed in insert mode: a register name is next

// What a key adds to (or takes from) the text typed in this insert session:
// feeds the . register. Once per key, outside mc_each.
void record_insert_key(int c) {
    switch(c) {
        case '\r': reg_insert_typed("\n"); break;
        case BACKSPACE: reg_insert_erase(RegErase::Char); break;
        case CTRL_BACKSPACE:
        case CTRL_KEY('h'):
        case CTRL_KEY('w'): reg_insert_erase(RegErase::Word); break;
        case CTRL_KEY('u'): reg_insert_erase(RegErase::All); break;
        default:
            // printable ASCII, a tab, or one byte of a UTF-8 character
            if((c >= 32 && c < 127) || c == '\t' || (c >= 0x80 && c < 0x100))
                reg_insert_typed(std::string(1, static_cast<char>(c)));
            break;
    }
}

// <C-r>x inserts register x. Handled once per key, outside mc_each, so every
// cursor gets the text but the pending flag is only seen one time.
bool insert_register_key(int c) {
    if(g_wait_reg) {
        g_wait_reg = false;
        if(c < 0x100 && reg_name_ok(static_cast<char>(c))) {
            const RegValue v = reg_read(static_cast<char>(c));
            if(!v.text.empty()) {
                reg_insert_typed(v.text);  // recorded once, outside mc_each
                mc_each([&] {
                    const auto own = reg_mc_slot(static_cast<char>(c));
                    editor_paste(own ? own->text : v.text);
                });
            }
        }
        return true;
    }
    if(c == CTRL_KEY('r')) {
        g_wait_reg = true;
        return true;
    }
    return false;
}

// Snippet keys act on the snippet once per key, not once per cursor: Tab goes
// to the next stop (the other cursors follow it there), typing over a
// selected placeholder clears it. True when the key is used up.
bool snippet_key_once(int c) {
    if(c == '\t' && snippet_active()) {
        snippet_next();
        return true;
    }
    return snippet_pre_key(c);
}

// The old INSERT/REPLACE body, extracted so chord replay uses the same path.
void insert_literal(int c) {
    if(c == '\t' && snippet_try_trigger()) return;
    if(E.mode == REPLACE && replace_key(c)) return;

    switch(c) {
        case '\r': editor_insert_newline(); break;
        case BACKSPACE: editor_delete_char(); break;
        case CTRL_BACKSPACE:
        case CTRL_KEY('h'):
        case CTRL_KEY('w'): editor_delete_word_back(); break;
        case DELETE_WORD: editor_delete_word_forward(); break;
        case CTRL_KEY('u'): editor_kill_to_bol(); break;
        case CTRL_KEY('k'): editor_kill_to_eol(); break;
        case CTRL_KEY('a'):
        case CTRL_KEY('e'):
            E.buf().pt.end_group();
            E.cx = c == CTRL_KEY('a') ? 0 : static_cast<int>(line_len(E.cy));
            E.want_rx = cx_to_rx(E.buf().pt.line_text(E.cy), E.cx);
            E.buf().pt.begin_group();
            break;
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
    const int c = key_passthrough(key);
    if(c == NONE) return;

    const EditorMode before = E.mode;
    const uint64_t rev = E.buf().pt.revision();
    if(!snippet_key_once(c)) mc_each([&] { insert_literal(c); });
    snippet_post_key();
    completion_post_key(c);
    dot_after_key(c, before, rev);
}

}  // namespace

static int g_inject_depth = 0;

void editor_handle_key(int c) {
    // A focused terminal panel gets every key, paste included, before the
    // PASTE branch and the modifier gate below can drop or reshape it.
    if(dock_raw_key(c)) return;
    // The `key` event: typed keys only, so a handler that injects keys can't
    // trigger itself.
    if(g_inject_depth == 0 && !dot_replaying() && events_has("key"))
        events_emit("key", {format_keys(KeySeq(1, static_cast<char32_t>(c)))});
    if(c == PASTE) {
        g_insert_chord.reset();
        if(overlay_modal()) {
            editor_take_paste();
            return;
        }
        const EditorMode before = E.mode;
        const uint64_t rev = E.buf().pt.revision();
        normal_reset();
        const std::string text = editor_take_paste();
        if(insertish(E.mode)) reg_insert_typed(text);
        mc_each([&] { editor_paste(text); });
        dot_supress();
        dot_after_key(c, before, rev);
        return;
    }
    if(c == '\x1b' && mc_active() && E.mode == NORMAL &&
       !normal_input_pending()) {
        mc_clear();
        return;
    }

    // Text-entry mappings run before the modifier gate, so <S-Space> and
    // <C-Space> can start a chord.
    if(!overlay_modal() && insertish(E.mode)) {
        if(g_insert_chord.feed(E.mode, c, insert_replay, E.buf().id) !=
           Chord::Result::Pass)
            return;
    }

    if(key_mods(c)) {
        const bool keymapped =
            !overlay_modal() &&
            (E.mode == NORMAL || E.mode == VISUAL || E.mode == FILES);
        if(!(keymapped && normal_accepts(c))) {
            c = insertish(E.mode) && !overlay_modal() ? key_basic(c)
                                                      : key_passthrough(c);
            if(c == NONE) return;
        }
    }
    if(c != PASTE && overlay_key(c)) return;
    if(c != PASTE && dock_key(c)) return;
    const EditorMode before = E.mode;
    const uint64_t rev_before = E.buf().pt.revision();
    if(E.mode == FILES && (c == '\x1b' || c == CTRL_KEY('l')) &&
       tree_visual_cancel())
        return;  // Esc in a tree range: drop the range, stay in the tree

    switch(c) {
        case CTRL_KEY('s'): editor_save(); break;

        case HOME: mc_each([] { E.cx = 0; }); break;
        case END:
            mc_each([] {
                if(E.cy < E.num_rows()) E.cx = static_cast<int>(line_len(E.cy));
            });
            break;
        case ARROW_UP: mc_each([&] { editor_move_cursor(c); }); break;
        case ARROW_DOWN: mc_each([&] { editor_move_cursor(c); }); break;
        case ARROW_LEFT: mc_each([&] { editor_move_cursor(c); }); break;
        case ARROW_RIGHT: mc_each([&] { editor_move_cursor(c); }); break;

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

        case CTRL_KEY('l'):
        case '\x1b': {
            snippet_end();
            const bool was_insert = insertish(E.mode);
            const bool was_visual = E.mode == VISUAL;
            mc_each([&] {
                set_mode(EditorMode::NORMAL);  // only the first one flips it
                if(was_insert && E.cx > 0)
                    E.cx = static_cast<int>(
                        gr_prev(E.buf().pt.line_text(E.cy), E.cx));
                else if(was_visual)
                    set_cursor_offset(
                        motion::clamp_normal(E.buf().pt, cursor_offset()));
                update_want();
            });
        } break;
    }
    switch(E.mode) {
        case INSERT:
        case REPLACE:
            if(!insert_register_key(c)) {
                record_insert_key(c);
                if(!snippet_key_once(c))
                    mc_each([&] { insert_literal(c); });
            }
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
        case TERMINAL: break;
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
    const bool chord_pending = normal_chord_pending() ||
                               g_insert_chord.pending() || dock_chord_pending();
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
            else if(dock_chord_pending()) dock_chord_timeout();
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

bool editor_inject_keys(const std::vector<int>& keys, bool complete) {
    if(g_inject_depth >= 16) return false;
    ++g_inject_depth;
    const bool was_replaying = dot_set_replaying(true);
    const EditorMode mode_before = E.mode;
    const bool pending_before = normal_input_pending();
    for(const int k : keys) editor_handle_key(k);
    if(complete) {
        // only end what these keys started: a caller already in insert mode
        // stays there
        const bool left_open =
            (E.mode != mode_before && E.mode != NORMAL && E.mode != VISUAL) ||
            (!pending_before && normal_input_pending());
        if(left_open) editor_handle_key(27);
    }
    dot_set_replaying(was_replaying);
    --g_inject_depth;
    return true;
}

void editor_feed_keys(std::vector<int> keys) {
    loop_post([keys = std::move(keys)] {
        for(const int k : keys) editor_handle_key(k);
    });
}
