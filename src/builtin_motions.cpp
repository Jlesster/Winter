#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>

#include "builtin.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "piece.hpp"
#include "registry.hpp"

namespace {

// a pure motion: usable by operators (via the motion table) and by the
// cursor (via the action of the same name)
using Pure = Target (*)(const PieceTable&, size_t, int);

motion::Fn wrap(Pure fn) {
    return [fn](const PieceTable& t, size_t p, int c, std::string_view) {
        return fn(t, p, c);
    };
}

void map_shared(const char* name, const char* keys) {
    const std::string k = parse_keys(keys);
    registry().map(NORMAL, parse_keys(keys), name);
    registry().map(OPPEND, parse_keys(keys), name);
    registry().map(VISUAL, parse_keys(keys), name);
}

void bind_op_motion(const char* name, Pure fn, const char* keys) {
    registry().add_motion(name, wrap(fn));
    if(*keys) registry().map(OPPEND, parse_keys(keys), name);
}

void bind_motion(const char* name, Pure fn, const char* keys) {
    registry().add_motion(name, wrap(fn));
    registry().add_action(name, [fn](Ctx& c) {
        apply_motion(fn(E.pt, cursor_offset(), c.count));
    });
    map_shared(name, keys);
}

void bind_action(const char* name, Action fn, const char* keys,
                 bool wants_char = false) {
    registry().add_action(name, std::move(fn), wants_char);
    map_shared(name, keys);
}

struct FindMemory {
    bool valid = false;
    motion::Find kind = motion::Find::f;
    std::string ch;
};
FindMemory last_find;  // what ; and , repeat

void bind_find(const char* name, motion::Find kind, const char* keys) {
    bind_action(
        name,
        [kind](Ctx& c) {
            last_find = {true, kind, c.arg};
            apply_motion(motion::find_char(
                E.pt, cursor_offset(), c.count, kind, c.arg, false));
        },
        keys,
        /*wants_char=*/true);
    registry().add_motion(
        name,
        [kind](const PieceTable& t, size_t p, int c, std::string_view arg) {
            last_find = {true, kind, std::string(arg)};
            return motion::find_char(t, p, c, kind, arg, false);
        });
}

void bind_repeat(const char* name, bool reverse, const char* keys) {
    bind_action(
        name,
        [reverse](Ctx& c) {
            if(!last_find.valid) return;
            const auto k =
                reverse ? motion::flip(last_find.kind) : last_find.kind;
            apply_motion(motion::find_char(
                E.pt, cursor_offset(), c.count, k, last_find.ch, true));
        },
        keys);
    registry().add_motion(
        name,
        [reverse](const PieceTable& t, size_t p, int c, std::string_view) {
            if(!last_find.valid)
                return Target{p, MKind::Exclusive, false, false};
            const auto k =
                reverse ? motion::flip(last_find.kind) : last_find.kind;
            return motion::find_char(t, p, c, k, last_find.ch, true);
        });
}

}  // namespace

void register_builtin_motions() {
    bind_op_motion("motion.line", motion::line_n, "");
    bind_op_motion("motion.down", motion::down, "j");
    bind_op_motion("motion.up", motion::up, "k");
    bind_op_motion("motion.right", motion::right_op, "");
    bind_motion("motion.left", motion::left, "h");
    bind_motion("motion.right", motion::right, "l");
    bind_motion("motion.line_start", motion::to_line_start, "0");
    bind_motion("motion.first_nonblank", motion::to_first_nonblank, "^");
    bind_motion("motion.line_end", motion::to_line_end, "$");
    bind_motion("motion.word_next", motion::next_word, "w");
    bind_motion("motion.WORD_next", motion::next_WORD, "W");
    bind_motion("motion.word_prev", motion::prev_word, "b");
    bind_motion("motion.WORD_prev", motion::prev_WORD, "B");
    bind_motion("motion.word_end", motion::end_word, "e");
    bind_motion("motion.WORD_end", motion::end_WORD, "E");
    bind_motion("motion.file_start", motion::to_file_start, "gg");
    bind_motion("motion.file_end", motion::to_file_end, "G");
    bind_motion("motion.para_back", motion::para_back, "{");
    bind_motion("motion.para_fwd", motion::para_fwd, "}");
    bind_motion("motion.match_pair", motion::match_pair, "%");

    registry().add_action("visual.right", [](Ctx& c) {
        apply_motion(motion::right_op(E.pt, cursor_offset(), c.count));
    });
    registry().map(VISUAL, "l", "visual.right");
    registry().add_action("visual.line_end", [](Ctx& c) {
        Target t = motion::to_line_end(E.pt, cursor_offset(), c.count);
        if(!t.ok) return;
        t.pos = E.pt.line_end(E.pt.line_of(t.pos));
        apply_motion(t);
    });
    registry().map(VISUAL, "$", "visual.line_end");

    // j / k need the goal column, so they are actions only for now
    bind_action("motion.down", [](Ctx& c) { move_vertical(+1, c.count); }, "j");
    bind_action("motion.up", [](Ctx& c) { move_vertical(-1, c.count); }, "k");

    bind_find("find.f", motion::Find::f, "f");
    bind_find("find.F", motion::Find::F, "F");
    bind_find("find.t", motion::Find::t, "t");
    bind_find("find.T", motion::Find::T, "T");
    bind_repeat("find.repeat", false, ";");
    bind_repeat("find.repeat_reverse", true, ",");
}
