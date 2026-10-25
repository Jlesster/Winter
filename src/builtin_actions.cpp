#include <string>

#include "buflist.hpp"
#include "builtin.hpp"
#include "command.hpp"
#include "diag.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "registry.hpp"
#include "search.hpp"
#include "utf8.hpp"

namespace {

void bind(const char* name, Action fn, const char* keys) {
    registry().add_action(name, std::move(fn));
    registry().map(NORMAL, parse_keys(keys), name);
}

void bind_visual(const char* name, Action fn, const char* keys) {
    registry().add_action(name, std::move(fn));
    registry().map(VISUAL, parse_keys(keys), name);
}

void enter_visual(bool line) {
    E.v_anchor = cursor_offset();
    E.v_line = line;
    set_mode(VISUAL);
}

// move the cursor, then start inserting there
void enter_insert_at(int cx) {
    E.cx = cx;
    set_mode(INSERT);
}

}  // namespace

void register_builtin_actions() {
    bind("mode.insert", [](Ctx&) { set_mode(INSERT); }, "i");

    bind(
        "mode.append",
        [](Ctx&) {  // after the char under the cursor
            const std::string line = E.buf().pt.line_text(E.cy);
            const int size = static_cast<int>(line.size());
            enter_insert_at(E.cx < size
                                ? static_cast<int>(utf8_next(line, E.cx))
                                : E.cx);  // empty line: stay put
        },
        "a");

    bind(
        "mode.append_eol",
        [](Ctx&) { enter_insert_at(static_cast<int>(line_len(E.cy))); },
        "A");

    bind(
        "mode.insert_bol",
        [](Ctx&) {  // first non-blank
            const std::string line = E.buf().pt.line_text(E.cy);
            size_t i = 0;
            while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            enter_insert_at(static_cast<int>(i));
        },
        "I");

    bind(
        "edit.open_below",
        [](Ctx&) {
            set_mode(
                INSERT);  // first: the new line joins the insert's undo group
            editor_open_below();
        },
        "o");
    bind(
        "edit.open_above",
        [](Ctx&) {
            set_mode(INSERT);
            editor_open_above();
        },
        "O");

    bind("mode.visual", [](Ctx&) { enter_visual(false); }, "v");
    bind("mode.visual_line", [](Ctx&) { enter_visual(true); }, "V");

    // inside visual: v/V switch kind or leave, o swaps the ends
    bind_visual(
        "visual.char",
        [](Ctx&) {
            if(E.v_line) E.v_line = false;
            else set_mode(NORMAL);
        },
        "v");
    bind_visual(
        "visual.line",
        [](Ctx&) {
            if(!E.v_line) E.v_line = true;
            else set_mode(NORMAL);
        },
        "V");
    bind_visual(
        "visual.swap",
        [](Ctx&) {
            const size_t cur = cursor_offset();
            set_cursor_offset(E.v_anchor);
            E.v_anchor = cur;
            update_want();
        },
        "o");

    bind("mode.command", [](Ctx&) { set_mode(COMMAND); }, ":");
    bind("mode.search", [](Ctx&) { set_mode(SEARCH); }, "/");

    // buffer control
    bind(
        "buffer.next",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "]b");
    bind(
        "buffer.prev",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "[b");
    bind(
        "buffer.next",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "<S-l>");
    bind(
        "buffer.prev",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "<S-l>");
    bind(
        "buffer.alt",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "<C-^>");

    // lsp control
    bind(
        "diag.next",
        [](Ctx& c) {
            dot_supress();
            diag_jump(+1, c.count);
        },
        "]d");
    bind(
        "diag.prev",
        [](Ctx& c) {
            dot_supress();
            diag_jump(-1, c.count);
        },
        "[d");

    bind("search.next", [](Ctx&) { editor_find_callback("", 'n'); }, "n");
    bind("search.prev", [](Ctx&) { editor_find_callback("", 'N'); }, "N");

    bind(
        "edit.undo",
        [](Ctx&) {
            dot_supress();
            if(auto at = E.buf().pt.undo()) {
                set_cursor_offset(motion::clamp_normal(E.buf().pt, *at));
                update_want();
            } else editor_set_status_message("Already at oldest change");
        },
        "u");
    bind(
        "edit.redo",
        [](Ctx&) {
            dot_supress();
            if(auto at = E.buf().pt.redo()) {
                set_cursor_offset(motion::clamp_normal(E.buf().pt, *at));
                update_want();
            } else editor_set_status_message("Already at newest change");
        },
        "<C-r>");
    bind("edit.dot", [](Ctx& c) { dot_replay(); }, ".");

    bind_visual(
        "visual.command",
        [](Ctx&) {
            set_mode(NORMAL);
            E.cmd_prefill = std::string(kVisualRange);
            set_mode(COMMAND);
        },
        ":");
}

void register_builtins() {
    register_builtin_motions();
    register_builtin_actions();
    register_builtin_operators();
    register_builtin_commands();
}
