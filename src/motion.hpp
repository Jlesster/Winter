#pragma once

#include <cstddef>
#include <string_view>

#include "piece.hpp"

enum class MKind { Exclusive, Inclusive, Linewise };

struct Target {
    size_t pos = 0;
    MKind kind = MKind::Exclusive;
    bool ok = true;
    bool to_eol = false;
};

namespace motion {

using Fn = Target (*)(const PieceTable&, size_t pos, int count);

Target left(const PieceTable&, size_t, int);
Target right(const PieceTable&, size_t, int);
Target to_line_start(const PieceTable&, size_t, int);
Target to_first_nonblank(const PieceTable&, size_t, int);
Target to_line_end(const PieceTable&, size_t, int);
Target next_word(const PieceTable&, size_t, int);
Target next_WORD(const PieceTable&, size_t, int);
Target prev_word(const PieceTable&, size_t, int);
Target prev_WORD(const PieceTable&, size_t, int);
Target end_word(const PieceTable&, size_t, int);
Target end_WORD(const PieceTable&, size_t, int);
Target to_file_start(const PieceTable&, size_t, int);
Target to_file_end(const PieceTable&, size_t, int);
Target para_back(const PieceTable&, size_t, int);
Target para_fwd(const PieceTable&, size_t, int);
Target match_pair(const PieceTable&, size_t, int);

enum class Find { f, F, t, T };
inline Find flip(Find k) {
    switch(k) {
        case Find::f: return Find::F;
        case Find::F: return Find::f;
        case Find::t: return Find::T;
        default: return Find::t;
    }
}

Target find_char(const PieceTable&, size_t pos, int count, Find kind,
                 std::string_view target, bool repeat);

size_t clamp_normal(const PieceTable&, size_t pos);

};  // namespace motion
