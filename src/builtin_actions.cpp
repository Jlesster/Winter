#include <cstddef>
#include <string>

#include "buflist.hpp"
#include "builtin.hpp"
#include "command.hpp"
#include "comment.hpp"
#include "complete.hpp"
#include "diag.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "jumplist.hpp"
#include "lsp_actions.hpp"
#include "lsp_rename.hpp"
#include "lsp_nav.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "registry.hpp"
#include "search.hpp"
#include "tree.hpp"
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

    bind("search.next", [](Ctx& c) { search_step(+1, c.count); }, "n");
    bind("search.prev", [](Ctx& c) { search_step(-1, c.count); }, "N");

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
    bind("edit.dot", [](Ctx&) { dot_replay(); }, ".");

    registry().add_action(
        "edit.replace_char",
        [](Ctx& c) { replace_char_at_cursor(c.count, c.arg); },
        true);
    registry().map(NORMAL, parse_keys("r"), "edit.replace_char");
    bind(
        "mode.replace",
        [](Ctx&) {
            set_mode(REPLACE);
            replace_reset();
        },
        "R");

    bind_visual(
        "visual.command",
        [](Ctx&) {
            const Range r = visual_range();
            E.cmd_sel_start = r.start;
            E.cmd_sel_end = r.end;
            E.cmd_sel = true;
            set_mode(NORMAL);
            E.cmd_prefill = std::string(kVisualRange);
            set_mode(COMMAND);
        },
        ":");

    bind("lsp.hover", [](Ctx&) { hover_show(); }, "K");
    bind("lsp.definition", [](Ctx&) { lsp_goto(NavKind::Definition); }, "gd");
    bind("lsp.definition", [](Ctx&) { lsp_goto(NavKind::Declaration); }, "gD");
    bind(
        "lsp.type_definition",
        [](Ctx&) { lsp_goto(NavKind::TypeDefinition); },
        "gy");
    bind(
        "lsp.implementation",
        [](Ctx&) { lsp_goto(NavKind::Implementation); },
        "gi");
    bind("lsp.references", [](Ctx&) { lsp_goto(NavKind::References); }, "gr");
    bind(
        "lsp.switch_source_header",
        [](Ctx&) { lsp_switch_source_header(); },
        "gh");
    bind("lsp.code_action", [](Ctx&) { lsp_code_action(); }, "<space>la");
    bind_visual(
        "lsp.code_action_visual", [](Ctx&) { lsp_code_action(); }, "<space>la");
    bind("lsp.rename", [](Ctx&) { lsp_rename(); }, "<space>lr");
    bind("jump.older", [](Ctx& c) { jump_older(c.count); }, "<C-o>");
    bind("jump.newer", [](Ctx& c) { jump_newer(c.count); }, "<Tab>");
    bind_visual(
        "visual.dup",
        [](Ctx&) {
            set_mode(NORMAL);
            editor_command_palette("'<,'>t'>");
        },
        "<space>dl");
    bind("tree.toggle", [](Ctx&) { tree_toggle(); }, "<space>e");
    registry().map(FILES, parse_keys("<space>e"), "tree.toggle");

    bind(
        "edit.comment",
        [](Ctx& c) {
            const size_t l0 = static_cast<size_t>(E.cy);
            comment_toggle(l0,
                           l0 + static_cast<size_t>(std::max(c.count, 1)) - 1);
        },
        "<space>/");

    bind_visual(
        "visual.comment",
        [](Ctx&) {
            const Range r = visual_range();
            const auto& pt = E.buf().pt;
            const size_t l0 = pt.line_of(r.start);
            const size_t l1 = pt.line_of(r.end > r.start ? r.end - 1 : r.start);
            set_mode(NORMAL);
            comment_toggle(l0, l1);
        },
        "<space>/");
}

void register_builtins() {
    register_builtin_motions();
    register_builtin_actions();
    register_builtin_operators();
    register_builtin_commands();
}
