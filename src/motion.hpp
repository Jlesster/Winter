#pragma once

#include <cstddef>
#include <string_view>

#include "piece.hpp"

// How an operator (d/c/y, next step) treats the span between the cursor and
// the target. Plain cursor movement ignores it.
enum class MKind { Exclusive, Inclusive, Linewise };

struct Target {
    size_t pos = 0;                 // byte offset the cursor lands on
    MKind kind = MKind::Exclusive;
    bool ok = true;                 // false: the motion failed, don't move
    bool to_eol = false;            // `$`: keep the cursor glued to line ends
};

// Every motion is a pure function: (text, where I am, count) -> target.
// count == 0 means "no count typed" (G and gg care about the difference).
namespace motion {

using Fn = Target (*)(const PieceTable&, size_t pos, int count);

Target left(const PieceTable&, size_t, int);              // h
Target right(const PieceTable&, size_t, int);             // l
Target to_line_start(const PieceTable&, size_t, int);     // 0
Target to_first_nonblank(const PieceTable&, size_t, int); // ^
Target to_line_end(const PieceTable&, size_t, int);       // $
Target next_word(const PieceTable&, size_t, int);         // w
Target next_WORD(const PieceTable&, size_t, int);         // W
Target prev_word(const PieceTable&, size_t, int);         // b
Target prev_WORD(const PieceTable&, size_t, int);         // B
Target end_word(const PieceTable&, size_t, int);          // e
Target end_WORD(const PieceTable&, size_t, int);          // E
Target to_file_start(const PieceTable&, size_t, int);     // gg
Target to_file_end(const PieceTable&, size_t, int);       // G
Target para_back(const PieceTable&, size_t, int);         // {
Target para_fwd(const PieceTable&, size_t, int);          // }
Target match_pair(const PieceTable&, size_t, int);        // %

enum class Find { f, F, t, T };
inline Find flip(Find k) {
    switch(k) {
        case Find::f: return Find::F;
        case Find::F: return Find::f;
        case Find::t: return Find::T;
        default: return Find::t;
    }
}
// `target` is one UTF-8 character. `repeat` is true for ; and ,
Target find_char(const PieceTable&, size_t pos, int count, Find kind,
                 std::string_view target, bool repeat);

// In normal mode the cursor never rests past the last character of a line.
size_t clamp_normal(const PieceTable&, size_t pos);

}  // namespace motion
