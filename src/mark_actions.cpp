// Marks in the editor: m{x} sets, '{x} and `{x} jump (also as operator
// motions: d'a, y`a), plus the hooks that keep the special marks fed.
#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "builtin.hpp"
#include "buflist.hpp"
#include "command.hpp"
#include "core.hpp"
#include "edit_bus.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "fold_buf.hpp"
#include "jumplist.hpp"
#include "marks.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "picker.hpp"
#include "registry.hpp"

namespace {

Buffer* buffer_by_id(uint64_t id) {
    for(const auto& b : E.buffers)
        if(b->id == id) return b.get();
    return nullptr;
}

char name_of(std::string_view arg) { return arg.size() == 1 ? arg[0] : 0; }

Target mark_target(const PieceTable& t, size_t p, std::string_view arg,
                   bool exact) {
    const Target fail{p, MKind::Exclusive, false, false};
    const auto m = mark_find(name_of(arg), E.buf().id);
    if(!m || m->buf != E.buf().id) return fail;  // another file: not a motion
    if(exact) return Target{std::min(m->off, t.size()), MKind::Exclusive};
    return Target{t.line_start(m->line), MKind::Linewise};
}

void bind(const char* name, const char* keys, bool jump_action, bool exact) {
    if(jump_action) {
        registry().add_action(
            name,
            [exact](Ctx& c) { jump_to_mark(name_of(c.arg), exact); },
            /*wants_char=*/true);
        registry().add_motion(
            name,
            [exact](const PieceTable& t, size_t p, int, std::string_view arg) {
                return mark_target(t, p, arg, exact);
            });
        const KeySeq k = parse_keys(keys);
        registry().map(NORMAL, k, name);
        registry().map(OPPEND, k, name);
        registry().map(VISUAL, k, name);
        return;
    }
    registry().add_action(
        name,
        [](Ctx& c) {
            const char n = name_of(c.arg);
            if(!mark_name_settable(n)) {
                editor_set_status_message("Invalid mark name");
                return;
            }
            mark_put(n, E.buf().id, cursor_offset());
        },
        /*wants_char=*/true);
    const KeySeq k = parse_keys(keys);
    registry().map(NORMAL, k, name);
    registry().map(VISUAL, k, name);
}

}  // namespace

// '{x} / `{x}: go to a mark; false (with a message) when there is none.
bool jump_to_mark(char name, bool exact) {
    if(!mark_name_valid(name)) {
        editor_set_status_message("Invalid mark name");
        return false;
    }
    const auto m = mark_find(name, E.buf().id);
    if(!m) {
        editor_set_status_message("Mark not set: {}", name);
        return false;
    }
    jump_push();  // after the lookup: '' must read the OLD '' before this sets it
    if(m->buf == 0) {  // its buffer was closed: reopen the file
        if(!jump_goto(m->file, m->line, exact ? m->col : 0)) {
            editor_set_status_message("Can't open {}", m->file);
            return false;
        }
        if(!exact)
            apply_motion(motion::to_first_nonblank(
                E.buf().pt, E.buf().pt.line_start(E.cy), 0));
    } else {
        if(m->buf != E.buf().id) {
            for(size_t i = 0; i < E.buffers.size(); ++i)
                if(E.buffers[i]->id == m->buf) {
                    buffer_switch(i);
                    break;
                }
            if(E.buf().id != m->buf) return false;
        }
        const PieceTable& pt = E.buf().pt;
        apply_motion(exact ? Target{motion::clamp_normal(pt, m->off)}
                           : motion::to_first_nonblank(
                                 pt, pt.line_start(m->line), 0));
    }
    fold_reveal(kFoldJump);
    return true;
}

void register_builtin_marks() {
    marks_set_host({[](uint64_t id) -> PieceTable* {
                        Buffer* b = buffer_by_id(id);
                        return b ? &b->pt : nullptr;
                    },
                    [](uint64_t id) {
                        const Buffer* b = buffer_by_id(id);
                        return b ? b->filename : std::string{};
                    }});

    bind("mark.set", "m", false, false);
    bind("mark.jump_line", "'", true, false);
    bind("mark.jump", "`", true, true);

    command_add({.name = "marks",
                 .desc = "List marks and jump to one",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) {
                     PickerSpec s;
                     s.prompt = "Marks> ";
                     s.on_accept = [](const PickItem& it) {
                         jump_to_mark(static_cast<char>(it.id), true);
                     };
                     picker_open(std::move(s));
                     std::vector<PickItem> items;
                     for(const char c : mark_names(E.buf().id)) {
                         const auto m = mark_find(c, E.buf().id);
                         if(!m) continue;
                         const Buffer* b = buffer_by_id(m->buf);  // null: closed
                         items.push_back(
                             {.text = std::format(
                                  "{}  {}:{}  {}", c,
                                  m->file.empty() ? "[No Name]" : m->file,
                                  m->line + 1,
                                  b ? b->pt.line_text(m->line) : std::string{}),
                              .id = static_cast<size_t>(c)});
                     }
                     picker_add(std::move(items));
                 }});

    // a closing buffer takes its marks with it; file marks remember the spot
    events_on("buf_close", [](const std::vector<std::string>&) {
        if(const Buffer* b = events_buf()) mark_buffer_closing(b->id);
    });
    // '.': where the buffer last changed. Run after the edit finished, so the
    // anchors are free to move.
    edit_subscribe(prio::decor + 7, [](const EditEvent& ev) {
        for(const auto& b : E.buffers) {
            if(&b->pt != ev.src) continue;
            edit_defer([id = b->id, pos = ev.pos] { mark_changed(id, pos); });
            return;
        }
    });
}
