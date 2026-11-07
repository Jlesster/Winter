#pragma once
// Folding glue: provider order, lazy recompute, cursor invariants, the
// reveal choke point and the z chords. The model itself is fold.hpp.

#include <string>
#include <string_view>
#include <vector>

struct Buffer;

// foldopen: which cursor jumps open the folds they land in (vim's 'foldopen')
enum FoldOpen : unsigned {
    kFoldSearch = 1,
    kFoldJump = 2,  // G gg % { } and jumplist moves
    kFoldUndo = 4,
    kFoldInsert = 8,
    kFoldQuickfix = 16,
    kFoldLsp = 32,
    kFoldAll = 63,
};

struct FoldConfig {
    // first non-empty provider wins: "treesitter", "indent" (later: "lsp")
    std::vector<std::string> providers{"treesitter", "indent"};
    unsigned open = kFoldAll;
    int minlines = 1;     // a fold must hide at least this many lines
    int levelstart = -1;  // >= 0: close folds at this depth+ when first built
    int text_closer = 1;  // append the closing line (`}`) to the fold text
};
FoldConfig& fold_config();

// Recompute the buffer's ranges if edits made them stale.
void fold_ensure(Buffer& b);

// The cursor never rests inside a closed fold: snap it to the head.
void fold_snap_cursor();

// The single place a cursor jump opens the folds around it. No-op when `why`
// is not in foldopen. Returns true if something opened.
bool fold_reveal(unsigned why);

// While held, entering insert mode doesn't open the fold (`o` on a head).
void fold_hold_reveal(bool hold);

std::string fold_text(int head, int last, std::string_view glyph = "⋯");

bool closers_only(std::string_view s);

void register_fold_actions();
