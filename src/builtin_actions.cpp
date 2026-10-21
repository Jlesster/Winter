#include <string>

#include "builtin.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "normal.hpp"
#include "registry.hpp"
#include "search.hpp"
#include "utf8.hpp"

namespace {

void bind(const char* name, Action fn, const char* keys) {
    registry().add_action(name, std::move(fn));
    registry().map(NORMAL, parse_keys(keys), name);
}

// move the cursor, then start inserting there
void enter_insert_at(int cx) {
    E.cx = cx;
    set_mode(INSERT);
}

}  // namespace

void register_builtin_actions() {
    bind("mode.insert", [](Ctx&) { set_mode(INSERT); }, "i");

    bind("mode.append", [](Ctx&) {  // after the char under the cursor
        const std::string line = E.pt.line_text(E.cy);
        const int size = static_cast<int>(line.size());
        enter_insert_at(E.cx < size ? static_cast<int>(utf8_next(line, E.cx))
                                    : E.cx);  // empty line: stay put
    }, "a");

    bind("mode.append_eol", [](Ctx&) {
        enter_insert_at(static_cast<int>(line_len(E.cy)));
    }, "A");

    bind("mode.insert_bol", [](Ctx&) {  // first non-blank
        const std::string line = E.pt.line_text(E.cy);
        size_t i = 0;
        while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        enter_insert_at(static_cast<int>(i));
    }, "I");

    bind("edit.open_below", [](Ctx&) {
        set_mode(INSERT);  // first: the new line joins the insert's undo group
        editor_open_below();
    }, "o");
    bind("edit.open_above", [](Ctx&) {
        set_mode(INSERT);
        editor_open_above();
    }, "O");

    bind("mode.visual", [](Ctx&) { set_mode(VISUAL); }, "v");
    bind("mode.command", [](Ctx&) { set_mode(COMMAND); }, ":");
    bind("mode.search", [](Ctx&) { set_mode(SEARCH); }, "/");

    bind("search.next", [](Ctx&) { editor_find_callback("", 'n'); }, "n");
    bind("search.prev", [](Ctx&) { editor_find_callback("", 'N'); }, "N");

    bind("edit.undo", [](Ctx&) {
        if(auto at = E.pt.undo()) {
            set_cursor_offset(*at);
            update_want();
        } else editor_set_status_message("Already at oldest change");
    }, "u");
    bind("edit.redo", [](Ctx&) {
        if(auto at = E.pt.redo()) {
            set_cursor_offset(*at);
            update_want();
        } else editor_set_status_message("Already at newest change");
    }, "<C-r>");
}

void register_builtins() {
    register_builtin_motions();
    register_builtin_actions();
}
