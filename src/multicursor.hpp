#pragma once
// multi cursor stuff, the primary cursor is the editors E.cx/E.cy
// the extras live here as PT anchors, so edits made at one curs is moves the
// others
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

bool mc_active();  // is multicursor active
void mc_clear();   // drop extra curs
// replace the extra cursors by new ones at these offsets (not inside mc_each)
void mc_set_extras(const std::vector<size_t>& offsets);

// run fn once per cursor, each in turn as *the* curs, one undo step
void mc_each(const std::function<void()>& fn);
// checks if the reg action acts once per cursor; (undo, :, lsp.. do not)
bool mc_action_multi(std::string_view action);

// Only meaningful inside fn of mc_each (mc_running()): which cursor runs now,
// in document order, and how many there are. mc_pass() changes with every
// mc_each that really ran more than one cursor.
bool mc_running();
size_t mc_index();
size_t mc_count();
bool mc_is_primary();  // inside mc_each: is this the cursor the user sees
uint64_t mc_pass();

void mc_add_next();           // select word / add cursor at the next match
void mc_skip_next();          // like add_next, but the current match is dropped
void mc_add_all();            // a cursor on every match of the selection
void mc_remove_last();        // undo the last cursor that was added
void mc_add_vertical(int d);  // +1 below, -1 above, same column
void mc_rotate(int d);        // make the next (+1) / previous (-1) one primary
void mc_split_lines();        // visual: one cursor per selected line

struct McMark {
    size_t pos;     // cursor
    size_t anchor;  // visual anchor (== pos outside visual mode)
};
std::vector<McMark> mc_marks();  // the extras, for the renderer
