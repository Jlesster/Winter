#pragma once
// Window layout: a tree of splits. Pure data and geometry; the editor
// integration (view registers, rendering, commands) lives in window_ed.cpp.
//
// A Row holds windows side by side (:vsplit), separated by one column. A Col
// stacks them (:split); every window carries its own status row as the last
// row of its rect, so stacked windows need no separator.
#include <cstdint>
#include <vector>

struct Buffer;

namespace win {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

// One window. The view registers are the editor's globals while it has focus
// (E.cx ...) and are parked here otherwise, exactly like switch_to() parks
// them in a Buffer.
struct Win {
    int id = 0;
    Buffer* buf = nullptr;
    int cx = 0, cy = 0, want_rx = 0;
    int row_offset = 0, row_sub = 0, col_offset = 0;
    Rect rect;  // including the status row
};

enum class Dir : uint8_t { Left, Down, Up, Right };

void init(Buffer* b);  // the first window; no-op once there is one
bool inited();
Win* focus();
Win* previous();  // the window that had focus before this one, or null
const std::vector<Win*>& all();  // left to right, top to bottom
size_t count();

// Assigns every rect inside `area` and rebuilds separators().
void layout(Rect area);
const std::vector<Rect>& separators();  // vertical, one column wide

void set_focus(Win* w);  // bookkeeping only; see window_ed.cpp for the swap

// New window on the same buffer, copying the view. It goes above / left of
// the current one when `before` is set (nvim's default) and takes focus.
Win* split(bool vertical, bool before = true);
// Removes `w`; returns the window that should take focus, or null when `w`
// was the only one (nothing is removed then).
Win* close(Win* w);
void only();  // closes every window but the focused one

// Window next to the focused one in direction `d`. (hint_x, hint_y) is the
// cursor's screen cell: it picks among several neighbours. Null = at the edge.
Win* neighbor(Dir d, int hint_x, int hint_y);

bool swap_next();       // CTRL-W x; focus follows the position
bool rotate(bool fwd);  // CTRL-W r / R; only inside an all-leaf row or column
void move_far(Dir d);   // CTRL-W H J K L: full height or width at that edge
void resize(bool width, int delta);
bool resize_dir(Dir d, int n);
void equalize();
void maximize(bool width);

// Windows showing `b`.
std::vector<Win*> showing(const Buffer* b);

}  // namespace win
