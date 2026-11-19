#pragma once
// Marks: m{a-zA-Z} sets one, '{x} jumps to its line, `{x} to its exact spot.
//
//   a-z    this buffer; follows the text as it is edited
//   A-Z    file marks: usable from any buffer, remembered after the buffer closes
//   ' `    where the latest jump started (so '' swaps back and forth)
//   .      where this buffer was last changed          (set by the editor)
//   < >    start / end of this buffer's last visual selection (set by the editor)
//
// A mark inside text that gets deleted slides to where the deletion began.
// Positions here are 0-based; this core does not know about the editor, it
// reaches buffers through the host the editor installs.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class PieceTable;

struct MarkHost {
    std::function<PieceTable*(uint64_t)> table;    // buffer id -> text (null: gone)
    std::function<std::string(uint64_t)> file;     // buffer id -> its file name
};
void marks_set_host(MarkHost host);

struct MarkPos {
    uint64_t buf = 0;  // 0: the buffer is closed; open `file` and go to line/col
    std::string file;
    size_t line = 0, col = 0;
    size_t off = 0;  // byte offset; only meaningful when buf != 0
};

// Names a user may set / jump to. '`' is the same mark as '\''.
bool mark_name_settable(char c);
bool mark_name_valid(char c);

bool mark_put(char name, uint64_t buf, size_t off);  // false: bad name, no such buffer
// Same, at a (0-based) line and byte column; both are clamped into the text.
bool mark_put_at(char name, uint64_t buf, size_t line, size_t col);
std::optional<MarkPos> mark_find(char name, uint64_t cur_buf);
bool mark_del(char name, uint64_t cur_buf);
std::vector<char> mark_names(uint64_t cur_buf);  // set marks: a-z . < >, A-Z, '
// line -> letter of the a-zA-Z mark to show in the gutter (this buffer only)
std::map<size_t, char> mark_lines(uint64_t buf);

// the editor feeds these
void mark_prev(uint64_t buf, size_t off);              // before a jump
void mark_changed(uint64_t buf, size_t off);           // after an edit
void mark_visual(uint64_t buf, size_t a, size_t b);    // leaving visual mode
void mark_buffer_closing(uint64_t buf);
