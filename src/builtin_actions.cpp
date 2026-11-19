#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "buflist.hpp"
#include "builtin.hpp"
#include "command.hpp"
#include "comment.hpp"
#include "complete.hpp"
#include "core.hpp"
#include "diag.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "fold_buf.hpp"
#include "grapheme.hpp"
#include "jumplist.hpp"
#include "lsp_actions.hpp"
#include "lsp_nav.hpp"
#include "lsp_rename.hpp"
#include "motion.hpp"
#include "multicursor.hpp"
#include "normal.hpp"
#include "output.hpp"
#include "registry.hpp"
#include "search.hpp"
#include "tree.hpp"
#include "window.hpp"
#include "window_ed.hpp"

namespace {

void bind_key(const char* name, Action fn, std::string_view keys) {
    registry().add_action(name, std::move(fn));
    registry().map(NORMAL, parse_keys(keys), name);
}

void bind_visual(const char* name, Action fn, std::string_view keys) {
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
    bind_key("mode.insert", [](Ctx&) { set_mode(INSERT); }, "i");

    bind_key(
        "mode.append",
        [](Ctx&) {  // after the char under the cursor
            const std::string line = E.buf().pt.line_text(E.cy);
            const int size = static_cast<int>(line.size());
            enter_insert_at(E.cx < size ? static_cast<int>(gr_next(line, E.cx))
                                        : E.cx);  // empty line: stay put
        },
        "a");

    bind_key(
        "mode.append_eol",
        [](Ctx&) { enter_insert_at(static_cast<int>(line_len(E.cy))); },
        "A");

    bind_key(
        "mode.insert_bol",
        [](Ctx&) {  // first non-blank
            const std::string line = E.buf().pt.line_text(E.cy);
            size_t i = 0;
            while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            enter_insert_at(static_cast<int>(i));
        },
        "I");

    bind_key(
        "edit.open_below",
        [](Ctx&) {
            fold_hold_reveal(true);  // the fold stays closed under the new line
            set_mode(
                INSERT);  // first: the new line joins the insert's undo group
            fold_hold_reveal(false);
            editor_open_below();
        },
        "o");
    bind_key(
        "edit.open_above",
        [](Ctx&) {
            fold_hold_reveal(true);
            set_mode(INSERT);
            fold_hold_reveal(false);
            editor_open_above();
        },
        "O");

    bind_key("mode.visual", [](Ctx&) { enter_visual(false); }, "v");
    bind_key("mode.visual_line", [](Ctx&) { enter_visual(true); }, "V");

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

    bind_key("mode.command", [](Ctx&) { set_mode(COMMAND); }, ":");
    bind_key("mode.search", [](Ctx&) { set_mode(SEARCH); }, "/");

    // buffer control
    bind_key(
        "buffer.next",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "]b");
    bind_key(
        "buffer.prev",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(-std::max(c.count, 1));
        },
        "[b");
    bind_key(
        "buffer.next",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "<S-l>");
    bind_key(
        "buffer.prev",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(-std::max(c.count, 1));
        },
        "<S-h>");
    bind_key(
        "buffer.alt",
        [](Ctx& c) {
            dot_supress();
            buffer_cycle(std::max(c.count, 1));
        },
        "<C-^>");

    // lsp control
    bind_key(
        "diag.next",
        [](Ctx& c) {
            dot_supress();
            diag_jump(+1, c.count);
        },
        "]d");
    bind_key(
        "diag.prev",
        [](Ctx& c) {
            dot_supress();
            diag_jump(-1, c.count);
        },
        "[d");

    bind_key("search.next", [](Ctx& c) { search_step(+1, c.count); }, "n");
    bind_key("search.prev", [](Ctx& c) { search_step(-1, c.count); }, "N");

    bind_key(
        "edit.undo",
        [](Ctx&) {
            dot_supress();
            if(auto at = E.buf().pt.undo()) {
                set_cursor_offset(motion::clamp_normal(E.buf().pt, *at));
                update_want();
                fold_reveal(kFoldUndo);
            } else editor_set_status_message("Already at oldest change");
        },
        "u");
    bind_key(
        "edit.redo",
        [](Ctx&) {
            dot_supress();
            if(auto at = E.buf().pt.redo()) {
                set_cursor_offset(motion::clamp_normal(E.buf().pt, *at));
                update_want();
                fold_reveal(kFoldUndo);
            } else editor_set_status_message("Already at newest change");
        },
        "<C-r>");
    bind_key("edit.dot", [](Ctx&) { dot_replay(); }, ".");

    registry().add_action(
        "edit.replace_char",
        [](Ctx& c) { replace_char_at_cursor(c.count, c.arg); },
        true);
    registry().map(NORMAL, parse_keys("r"), "edit.replace_char");
    bind_key(
        "mode.replace",
        [](Ctx&) {
            set_mode(REPLACE);
            replace_reset();
        },
        "R");

    bind_key(
        "edit.toggle_case",
        [](Ctx& c) { toggle_case_at_cursor(c.count); },
        "~");
    bind_visual(
        "visual.toggle_case",
        [](Ctx&) {
            const Range r = visual_range();
            const bool line = E.v_line;
            set_mode(NORMAL);
            toggle_case_in_range(r.start, r.end, line);
            set_cursor_offset(r.start);
            update_want();
        },
        "~");

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

    bind_key("lsp.hover", [](Ctx&) { hover_show(); }, "K");
    bind_key(
        "lsp.definition", [](Ctx&) { lsp_goto(NavKind::Definition); }, "gd");
    bind_key(
        "lsp.definition", [](Ctx&) { lsp_goto(NavKind::Declaration); }, "gD");
    bind_key(
        "lsp.type_definition",
        [](Ctx&) { lsp_goto(NavKind::TypeDefinition); },
        "gy");
    bind_key(
        "lsp.implementation",
        [](Ctx&) { lsp_goto(NavKind::Implementation); },
        "gi");
    bind_key(
        "lsp.references", [](Ctx&) { lsp_goto(NavKind::References); }, "gr");
    bind_key(
        "lsp.switch_source_header",
        [](Ctx&) { lsp_switch_source_header(); },
        "gh");
    bind_key("lsp.code_action", [](Ctx&) { lsp_code_action(); }, "<space>la");
    bind_visual(
        "lsp.code_action_visual", [](Ctx&) { lsp_code_action(); }, "<space>la");
    bind_key("lsp.rename", [](Ctx&) { lsp_rename(); }, "<space>lr");
    bind_key("jump.older", [](Ctx& c) { jump_older(c.count); }, "<C-o>");
    bind_key("jump.newer", [](Ctx& c) { jump_newer(c.count); }, "<Tab>");
    bind_visual(
        "visual.dup",
        [](Ctx&) {
            set_mode(NORMAL);
            editor_command_palette("'<,'>t'>");
        },
        "<space>dl");
    // Windows: the nvim CTRL-W family. Each direction is its own action, so
    // `winter.keymap` can put any key on win.left / win.down / win.up /
    // win.right (see also opts.tmux_nav).
    struct WinKey {
        const char* action;
        char key;
    };
    struct NumKey {
        const char* name;
        const char* keys;
        int sign;
        bool int_only;
    };
    struct VNumKey {
        const char* name;
        const char* keys;
        int sign;
        bool prog;
    };
    static constexpr WinKey kWin[] = {
        {"win.left", 'h'},      {"win.down", 'j'},
        {"win.up", 'k'},        {"win.right", 'l'},
        {"win.split", 's'},     {"win.vsplit", 'v'},
        {"win.close", 'c'},     {"win.only", 'o'},
        {"win.next", 'w'},      {"win.prev", 'W'},
        {"win.last", 'p'},      {"win.swap", 'x'},
        {"win.rotate", 'r'},    {"win.rotate_back", 'R'},
        {"win.move_left", 'H'}, {"win.move_bottom", 'J'},
        {"win.move_top", 'K'},  {"win.move_right", 'L'},
        {"win.equalize", '='},  {"win.taller", '+'},
        {"win.shorter", '-'},   {"win.wider", '>'},
        {"win.narrower", '<'},  {"win.max_height", '_'},
        {"win.max_width", '|'}};
    static constexpr NumKey kNum[] = {
        {"edit.increment", "<C-a>", +1, false},
        {"edit.decrement", "<C-x>", -1, false},
        {"edit.increment_int", "<A-a>", +1, true},  // INCREMENT
        {"edit.decrement_int", "<A-x>", -1, true},  // DECREMENT
    };
    static constexpr VNumKey kVNum[] = {
        {"visual.increment", "<C-a>", +1, false},
        {"visual.decrement", "<C-x>", -1, false},
        {"visual.increment_int", "g<C-a>", +1, true},  // INCREMENT
        {"visual.decrement_int", "g<C-x>", -1, true},  // DECREMENT
    };
    for(const WinKey& w : kWin) {
        const char k = w.key;
        bind_key(
            w.action,
            [k](Ctx& c) { win_do(k, c.count); },
            std::string("<C-w>") + k);
        // nvim also takes CTRL-W CTRL-{h,j,k,l,s,v,c,o,w,p,x,r}
        if(std::string_view("hjklsvcowpxr").find(k) != std::string_view::npos)
            registry().map(NORMAL,
                           parse_keys(std::string("<C-w><C-") + k + ">"),
                           w.action);
    }
    for(const NumKey& n : kNum) {
        const int64_t sign = n.sign;
        const bool ip = n.int_only;
        bind_key(
            n.name,
            [sign, ip](Ctx& c) {
                bump_number_at_cursor(sign * std::max(c.count, 1), ip);
            },
            n.keys);
    }
    for(const VNumKey& n : kVNum) {
        const int64_t sign = n.sign;
        const bool prog = n.prog;
        bind_visual(
            n.name,
            [sign, prog](Ctx& c) {
                const Range r = visual_range();
                const bool line = E.v_line;
                set_mode(NORMAL);
                int64_t delta = sign * std::max(c.count, 1);
                bool progressive = prog;
                if(prog && mc_running()) {  // 1st cursor +1, 2nd +2, ...
                    delta *= static_cast<int64_t>(mc_index()) + 1;
                    progressive = false;
                }
                bump_numbers_in_range(r.start, r.end, line, delta, progressive);
                set_cursor_offset(r.start);
                update_want();
            },
            n.keys);
    }
    bind_key(
        "win.resize_left",
        [](Ctx& c) { win_resize(win::Dir::Left, c.count); },
        "<A-h>");
    bind_key(
        "win.resize_down",
        [](Ctx& c) { win_resize(win::Dir::Down, c.count); },
        "<A-j>");
    bind_key(
        "win.resize_up",
        [](Ctx& c) { win_resize(win::Dir::Up, c.count); },
        "<A-k>");
    bind_key(
        "win.resize_right",
        [](Ctx& c) { win_resize(win::Dir::Right, c.count); },
        "<A-l>");
    // the tree is a region of the ring too: CTRL-W h/j/k/l leave it
    static constexpr WinKey kDirs[] = {{"win.left", 'h'},
                                       {"win.down", 'j'},
                                       {"win.up", 'k'},
                                       {"win.right", 'l'}};
    for(const WinKey& w : kDirs) {
        registry().map(
            FILES, parse_keys(std::string("<C-w>") + w.key), w.action);
        registry().map(
            TERMINAL, parse_keys(std::string("<C-w>") + w.key), w.action);
    }
    registry().add_action("mc.add_next", [](Ctx&) { mc_add_next(); });
    registry().add_action("mc.add_down", [](Ctx&) { mc_add_vertical(1); });
    registry().add_action("mc.add_up", [](Ctx&) { mc_add_vertical(-1); });
    registry().add_action("mc.clear", [](Ctx&) { mc_clear(); });
    registry().add_action("mc.skip_next", [](Ctx&) { mc_skip_next(); });
    registry().add_action("mc.add_all", [](Ctx&) { mc_add_all(); });
    registry().add_action("mc.remove_last", [](Ctx&) { mc_remove_last(); });
    registry().add_action("mc.next", [](Ctx&) { mc_rotate(+1); });
    registry().add_action("mc.prev", [](Ctx&) { mc_rotate(-1); });
    registry().add_action("mc.split_lines", [](Ctx&) { mc_split_lines(); });

    bind_key("output.toggle", [](Ctx&) { output_toggle(); }, "<space>O");
    bind_key("tree.toggle", [](Ctx&) { tree_toggle(); }, "<space>e");
    registry().map(FILES, parse_keys("<space>e"), "tree.toggle");

    bind_key(
        "edit.comment",
        [](Ctx& c) {
            int last = E.cy;
            for(int i = 1; i < std::max(c.count, 1); ++i)
                last = E.buf().folds.next_visible(last);
            comment_toggle(static_cast<size_t>(E.cy),
                           static_cast<size_t>(last));
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
    register_builtin_marks();
    register_builtin_commands();

    register_fold_actions();
}
