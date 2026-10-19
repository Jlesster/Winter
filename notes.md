# TUI Editor Notes (winter)

<!--toc:start-->

- [TUI Editor Notes (winter)](#tui-editor-notes-winter)
  - [1. Architecture in one page](#1-architecture-in-one-page)
  - [2. Piece tables and text buffers](#2-piece-tables-and-text-buffers)
    - [Papers and articles](#papers-and-articles)
    - [Implementations to read](#implementations-to-read)
    - [Key facts to remember](#key-facts-to-remember)
  - [3. Double buffering and cell diffing](#3-double-buffering-and-cell-diffing)
    - [How it works](#how-it-works)
    - [Copy vs swap](#copy-vs-swap)
    - [Wide characters](#wide-characters)
    - [Pending-wrap state](#pending-wrap-state)
    - [Optimizations to defer](#optimizations-to-defer)
    - [Reading](#reading)
  - [4. Terminal output and escape sequences](#4-terminal-output-and-escape-sequences)
  - [5. Input handling](#5-input-handling)
  - [6. Unicode, widths and graphemes](#6-unicode-widths-and-graphemes)
  - [7. Reference implementations (C++ first)](#7-reference-implementations-c-first)
    - [C++](#c)
    - [Other languages (still worth reading)](#other-languages-still-worth-reading)
  - [8. Testing](#8-testing)
  - [9. Suggested reading order](#9-suggested-reading-order)
  - [10. Appendix: code sketches from the design discussion](#10-appendix-code-sketches-from-the-design-discussion)
    - [Cell and Style](#cell-and-style)
    - [Screen](#screen)
    - [Flush](#flush)
    - [Piece table core](#piece-table-core)
    - [Line index maintenance](#line-index-maintenance)
    - [Undo by piece-list snapshot](#undo-by-piece-list-snapshot)

<!--toc:end-->

Reading list and design notes for a modal, nvim/vis-adjacent terminal editor in C++.
Items marked **[checked]** were looked up while compiling this file. Items marked
**[memory]** are well-known, stable URLs that were not re-fetched, so verify before
relying on them.

---

## 1. Architecture in one page

Three separate layers:

1. **Document model**: `vector<string>`, gap buffer, piece table or rope. Knows nothing about the screen.
2. **Cell grid**: a `back` and a `front` grid of cells. Rendering code only ever writes to `back`.
3. **Flush**: diffs `back` against `front`, emits minimal escape sequences in a single `write()`, then makes `back` the new `front`.

Rules worth keeping:

- The renderer and motions only call `line(i)` on the buffer, never the backing store directly, so the store can be swapped later.
- Tabs expand at render time. The buffer stores a real `\t`.
- Keep one function for byte index to screen column (`screen_col(line, byte_idx)`) and use it everywhere.
- Status line, gutter and command line are just more cells in the same grid.
- Undo is an edit log (or piece-list snapshots), designed in from day one.

---

## 2. Piece tables and text buffers

### Papers and articles

- **[checked]** Crowley, _Data Structures for Text Sequences_: https://www.cs.unm.edu/~crowley/papers/sds.pdf
  The canonical comparison of array, gap, list, line pointers, fixed-size buffers and piece tables. Read section 6.4 (piece table) first. HTML version of that section: https://www.cs.unm.edu/~crowley/papers/sds/node15.html
- **[checked]** VS Code, _Text Buffer Reimplementation_: https://code.visualstudio.com/blogs/2018/03/23/text-buffer-reimplementation
  The piece tree (balanced tree with cached newline counts). This is the upgrade path from a flat line-start vector. Source: https://github.com/microsoft/vscode-textbuffer
- **[checked]** catch22, _Piece Chains_: https://www.catch22.net/tuts/neatpad/piece-chains/
  Practical Win32 implementation walkthrough (James Brown).
- **[checked]** AbiWord, _Improving the AbiWord's Piece Table_: http://e98cuenc.free.fr/wordprocessor/piecetable.html
- **[checked]** Wikipedia, Piece table: https://en.wikipedia.org/wiki/Piece_table
- **[checked]** hummy123/ocaml-piecerope: https://github.com/hummy123/ocaml-piecerope
  A counterargument. It claims piece trees fragment (pieces only extend on consecutive inserts) and have slower substring and delete than a rope. Read before committing to a piece tree.
- **[checked]** xi-editor (persistent rope, copy-on-write snapshots): https://github.com/xi-editor/xi-editor
  Not actively developed any more, but the design is instructive: snapshotting the buffer for autosave is nearly free. Rope Science series: https://xi-editor.io/docs/rope_science_00.html **[memory]**
- **[memory]** Finseth, _The Craft of Text Editing_ (free online): https://www.finseth.com/craft/
  Old but still good on buffers and redisplay.

### Implementations to read

- **[memory]** vis (C piece chain, UTF-8, undo tree with `earlier`/`later`): https://github.com/martanne/vis
  The README's "Why another text editor?" section explains the design well. Closest match to what you are building.
- **[checked]** SweetEditor core (C++17, piece table, incremental layout, mmap large files): https://github.com/FinalScave/OpenSweetEditor
- **[checked]** simanw/PieceTableEditor (array vs splay tree implementation, with benchmarks against a naive editor): https://github.com/simanw/PieceTableEditor
- **[checked]** gvcode piece table (Go, based on the catch22 design, has grouped undo): https://pkg.go.dev/github.com/oligo/gvcode@v0.3.0/internal/buffer
- **[checked]** boj/zany piece table (Zig): https://github.com/boj/zany/blob/main/src/piece_table.zig
- **[memory]** Ropey (Rust rope used by Helix): https://github.com/cessen/ropey

### Key facts to remember

- Pieces store offsets, never pointers. The add buffer reallocates as it grows.
- `locate(pos)` should resolve a boundary to offset 0 of the next piece (`pos < len`, not `<=`).
- Coalesce typing: if the previous piece is an Add piece ending exactly at the old end of the add buffer, extend it instead of creating a piece.
- Never leave zero-length pieces.
- Line lookup needs its own index. A flat vector of line starts costs O(lines) per newline edit. A tree with cached newline counts removes that.
- A line spanning a piece boundary is not contiguous, so `line(i)` needs a scratch buffer with a zero-copy fast path.
- `mmap` of the original file risks `SIGBUS` if another process truncates it.

---

## 3. Double buffering and cell diffing

### How it works

- Two grids of cells: **front** (what the terminal currently shows) and **back** (what you are drawing for the next frame).
- All drawing goes to `back`. A flush function walks both, emits only the changed cells, then makes `back` the new `front`.
- Initialize `front` to a value that can never equal a real cell (for example `cp = U'\0'`). The first flush, and any flush after a resize, then repaints everything with no special-case code.
- Track the terminal cursor position and the current style while flushing. Skip cursor moves for adjacent changed cells, and only emit SGR when the style changes.
- Send the whole frame in one `write()`.
- Wrap the frame in synchronized output (`CSI ? 2026 h` ... `CSI ? 2026 l`) to avoid tearing where supported. Unsupported terminals ignore it.

### Copy vs swap

The sketch below copies `back` into `front` after each flush. If you clear and fully repaint `back` every frame, you can `std::swap(front, back)` instead, which is O(1). If you ever repaint only part of `back` per frame (dirty rows), a swap leaves stale cells in the new back buffer, so copy or clear first.

### Wide characters

A wide char takes a lead cell (`width = 2`) plus a continuation cell (`width = 0`). The diff skips continuation cells. When a narrow cell overwrites a wide one, the old continuation cell becomes a blank in `back` while `front` still holds `width == 0`. They differ, so the blank is drawn.

### Pending-wrap state

After writing the last column, terminals enter a pending-wrap state that behaves differently across emulators. Reset your tracked cursor position to "unknown" after writing the last column so the next write uses an explicit cursor move.

### Optimizations to defer

- Dirty-row or per-row damage ranges (see Termisu below, which bounds diff scans to changed column spans).
- Scroll regions (`DECSTBM` plus `CSI n S`/`T`) for scrolling large views.
- `REP` (repeat character) for runs of identical cells.
- A full 200x60 compare is about 12k cells, so none of this matters until measured.

### Reading

- **[checked]** termtui `buffer` module (Rust): https://docs.rs/termtui/latest/termtui/buffer/index.html and source https://docs.rs/termtui/latest/src/termtui/buffer.rs.html
  A small, readable double-buffer with front, back and a cell-update enum for diffs.
- **[checked]** Termisu `Buffer` (Crystal): https://crystaldoc.info/github/omarluq/termisu/v0.6.2/Termisu/Buffer.html
  Front and back buffers, per-row damage ranges to bound scans, `invalidate` to force a full redraw on the next render (the same trick as the sentinel `front` above), and a full-redraw call for resize or corruption.
- **[checked]** opentui_rust (Rust): https://docs.rs/crate/opentui_rust/latest/source/src/lib.rs
  Double-buffered renderer with diff detection, optional synchronized output, grapheme IDs for cells, and an input parser module. Good for seeing how the pieces fit together.
- **[checked]** dotmax animation guide (FrameBuffer): https://docs.rs/crate/dotmax/latest/source/docs/animation_guide.md
  Explains full render (O(width x height) writes) vs diff render (O(changed cells)) and an O(1) front/back swap.
- **[memory]** Ratatui `Buffer` and `Buffer::diff`: https://github.com/ratatui/ratatui (look under `ratatui-core/src/buffer/`)
  A compact version of the same approach, in a language you already use.
- **[memory]** notcurses rasterizer: https://github.com/dankamongmen/notcurses
  The most sophisticated cell-grid renderer in C. See its `docs/` and the rasterization code for wide-char and style-state handling.
- **[memory]** termbox2 (single-header C): https://github.com/termbox/termbox2

---

## 4. Terminal output and escape sequences

- **[checked]** Synchronized output (DEC mode 2026) spec: https://github.com/contour-terminal/vt-extensions (original gist: https://gist.github.com/christianparpart/d8a62cc1ab659194337d73e399004036)
  Purpose: avoid showing a half-drawn screen. The terminal applies everything between begin and end atomically, and it is a hint that may be ignored. Query support with `CSI ? 2026 $ p`. A reply of `2026 ; 0 $ y` means not supported.
- **[memory]** XTerm Control Sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
  Reference for CUP, SGR, DECSET, alternate screen (`?1049`), `REP`, and so on.
- **[memory]** terminfo: `man 5 terminfo`. Useful to skim even if you hardcode sequences.
- **[memory]** Kitty graphics and keyboard docs: https://sw.kovidgoyal.net/kitty/

Setup and teardown checklist:

- Alternate screen: `CSI ? 1049 h` on entry, `CSI ? 1049 l` on exit.
- Raw mode via `termios`, restored from an `atexit` handler and from signal handlers.
- `SIGWINCH`: set an atomic flag, handle it in the main loop (never call `resize()` from the handler).
- Render only when something changed (input, resize, async event), not on a timer.
- Hide the cursor during the flush, then place and show it at the end.

---

## 5. Input handling

- **[memory]** Paul Williams' DEC-compatible parser state machine: https://vt100.net/emu/dec_ansi_parser
  A proper state machine for escape sequences, instead of ad hoc `read()` logic.
- **[memory]** Kitty keyboard protocol: https://sw.kovidgoyal.net/kitty/keyboard-protocol/
  Lets a modal editor tell `Esc` from the start of an escape sequence, and `Ctrl-I` from `Tab`.
- **[memory]** Neovim UI protocol (grid-based redraw events): https://neovim.io/doc/user/ui.html
  A good model for a clean separation between editor state and rendering.
- Use a short timeout (or the Kitty protocol) to resolve a lone `Esc` from a sequence prefix.

---

## 6. Unicode, widths and graphemes

- **[memory]** UAX #11, East Asian Width: https://www.unicode.org/reports/tr11/ (basis for `wcwidth`)
- **[memory]** UAX #29, Grapheme Cluster Boundaries: https://www.unicode.org/reports/tr29/
- **[memory]** utf8proc (C): https://github.com/JuliaStrings/utf8proc
- **[memory]** Markus Kuhn's `wcwidth.c`: https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c

Notes:

- The buffer works in bytes. Column mapping lives in the renderer.
- Motions must only land on codepoint (later grapheme) boundaries.
- Combining marks and ZWJ emoji do not fit in one `char32_t`. A common fix is a flag bit in the cell meaning "index into an interned grapheme string table" (opentui above does something similar with grapheme IDs).

---

## 7. Reference implementations (C++ first)

### C++

- **[memory]** Kakoune: https://github.com/mawww/kakoune
  Modern C++ modal editor with a clean display-buffer and terminal-UI split.
- **[checked]** SweetEditor core (C++17, piece table, layout, large-file loading): https://github.com/FinalScave/OpenSweetEditor
- **[memory]** FTXUI (C++ TUI library; its `Screen` and `Pixel` types are a cell grid with diffing): https://github.com/ArthurSonzogni/FTXUI
- **[memory]** notcurses (C with a C++ wrapper): https://github.com/dankamongmen/notcurses

C++ language and library pieces used in the sketches:

- **[memory]** `std::format` / `std::format_to`: https://en.cppreference.com/w/cpp/utility/format
- **[memory]** Defaulted comparison (`operator== = default`, C++20): https://en.cppreference.com/w/cpp/language/default_comparisons
- **[memory]** `std::string_view`: https://en.cppreference.com/w/cpp/string/basic_string_view

### Other languages (still worth reading)

- **[memory]** vis (C): https://github.com/martanne/vis
- **[memory]** Helix (Rust, rope-based): https://github.com/helix-editor/helix
- **[memory]** Neovim: https://github.com/neovim/neovim (block-based line store in `memline.c`, grid UI)
- **[checked]** Go and Zig piece tables listed in section 2.

---

## 8. Testing

- Fuzz test the piece table against `std::string`: apply random inserts and erases to both, then compare the full text and the line-start vector after every step. This catches nearly all off-by-one errors in the erase cases.
- Assert `len > 0` for every piece in debug builds.
- Add a debug check that `total == sum(piece.len)` after every edit.
- Test the flush by running the diff against a tiny terminal-emulator model (a plain 2D array that interprets only CUP, SGR and text) and comparing its state to `back` after each frame.
- **[memory]** Catch2: https://github.com/catchorg/Catch2, doctest: https://github.com/doctest/doctest
- **[memory]** libFuzzer: https://llvm.org/docs/LibFuzzer.html, AFL++: https://github.com/AFLplusplus/AFLplusplus

---

## 9. Suggested reading order

1. Crowley section 6.4 plus the catch22 piece chain article.
2. vis README and the `ocaml-piecerope` critique, to see the tradeoffs in practice.
3. Ratatui `Buffer::diff` or the termtui and Termisu buffers, then the synchronized output spec, to check your flush loop.
4. XTerm control sequences and the Kitty keyboard protocol once you start on input.
5. The VS Code blog post only if you decide you need a piece tree.

---

## 10. Appendix: code sketches from the design discussion

These are sketches, not tested code. `cur_x`/`cur_y` in the flush are the editor cursor
position, and `emit_sgr`, `append_utf8` and `write_all` are helpers you still need to write.

### Cell and Style

```cpp
struct Style {
    uint32_t fg = 0xFFFFFFFF;   // sentinel = terminal default
    uint32_t bg = 0xFFFFFFFF;
    uint16_t attrs = 0;         // bold, italic, underline, reverse...
    bool operator==(const Style&) const = default;
};

struct Cell {
    char32_t cp    = U' ';
    uint8_t  width = 1;   // 1 normal, 2 wide lead, 0 = continuation of wide char to the left
    Style    style{};
    bool operator==(const Cell&) const = default;
};
```

### Screen

```cpp
class Screen {
    int w = 0, h = 0;
    std::vector<Cell> front, back;
    std::string out;                      // reused every frame
public:
    void resize(int nw, int nh) {
        w = nw; h = nh;
        back.assign(w * h, Cell{});
        front.assign(w * h, Cell{ U'\0' }); // never equal to a real cell: forces full repaint
    }
    Cell& at(int x, int y) { return back[y * w + x]; }
    void flush();
};
```

### Flush

```cpp
void Screen::flush() {
    out.clear();
    out += "\x1b[?2026h\x1b[?25l";          // sync-update begin, hide cursor
    int cx = -1, cy = -1;                   // where the terminal cursor is now
    Style cur{}; bool styleKnown = false;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            const Cell& b = back[i];
            if (b == front[i] || b.width == 0) continue;

            if (cy != y || cx != x)
                std::format_to(std::back_inserter(out), "\x1b[{};{}H", y + 1, x + 1);

            if (!styleKnown || b.style != cur) {
                emit_sgr(out, b.style);
                cur = b.style; styleKnown = true;
            }
            append_utf8(out, b.cp);
            cx = x + b.width; cy = y;
            if (cx >= w) cx = -1;           // pending-wrap is unreliable; force a CUP
        }
    }
    out += "\x1b[0m";
    std::format_to(std::back_inserter(out), "\x1b[{};{}H\x1b[?25h\x1b[?2026l", cur_y + 1, cur_x + 1);

    write_all(STDOUT_FILENO, out);          // ONE write() per frame
    front = back;                           // or std::swap if back is fully repainted each frame
}
```

### Piece table core

```cpp
enum class Src : uint8_t { Orig, Add };

struct Piece { Src src; size_t off; size_t len; };   // offsets, never pointers

class PieceTable {
    std::string_view   orig;      // immutable
    std::string        add;       // append-only
    std::vector<Piece> pieces;
    size_t             total = 0;
    std::vector<size_t> starts{0};  // line starts; starts[0] == 0
    // ...
};

struct Loc { size_t piece; size_t off; };

Loc locate(size_t pos) const {
    size_t i = 0;
    for (; i < pieces.size(); ++i) {
        if (pos < pieces[i].len) return {i, pos};
        pos -= pieces[i].len;
    }
    return {i, 0};
}
```

```cpp
void insert(size_t pos, std::string_view text) {
    if (text.empty()) return;
    const size_t add_off = add.size();
    add.append(text);
    total += text.size();

    auto [i, k] = locate(pos);

    if (k == 0 && i > 0) {                       // coalesce typing
        Piece& prev = pieces[i - 1];
        if (prev.src == Src::Add && prev.off + prev.len == add_off) {
            prev.len += text.size();
            return;
        }
    }

    Piece np{Src::Add, add_off, text.size()};
    if (k == 0) {
        pieces.insert(pieces.begin() + i, np);
    } else {
        Piece tail{pieces[i].src, pieces[i].off + k, pieces[i].len - k};
        pieces[i].len = k;
        pieces.insert(pieces.begin() + i + 1, {np, tail});
    }
}
```

```cpp
void erase(size_t pos, size_t n) {
    if (n == 0) return;
    n = std::min(n, total - pos);
    total -= n;

    auto [i, k] = locate(pos);
    size_t left = n;
    while (left && i < pieces.size()) {
        Piece& p = pieces[i];
        const size_t take = std::min(p.len - k, left);

        if (k == 0 && take == p.len) {                 // whole piece
            pieces.erase(pieces.begin() + i);
        } else if (k == 0) {                           // chop the front
            p.off += take; p.len -= take;
        } else if (k + take == p.len) {                // chop the back
            p.len -= take; ++i;
        } else {                                       // hole in the middle: split
            Piece tail{p.src, p.off + k + take, p.len - k - take};
            p.len = k;
            pieces.insert(pieces.begin() + i + 1, tail);
        }
        left -= take;
        k = 0;
    }
}
```

### Line index maintenance

```cpp
// after insert at pos:
auto it = std::upper_bound(starts.begin(), starts.end(), pos);
for (auto j = it; j != starts.end(); ++j) *j += text.size();
std::vector<size_t> fresh;
for (size_t p = 0; (p = text.find('\n', p)) != text.npos; ++p)
    fresh.push_back(pos + p + 1);
starts.insert(it, fresh.begin(), fresh.end());

// for erase at pos, length n (use the original n):
auto lo = std::upper_bound(starts.begin(), starts.end(), pos);
auto hi = std::upper_bound(starts.begin(), starts.end(), pos + n);
auto it = starts.erase(lo, hi);
for (; it != starts.end(); ++it) *it -= n;
```

### Undo by piece-list snapshot

```cpp
struct State { std::vector<Piece> pieces; size_t total; };
std::vector<State> undo_stack, redo_stack;

void checkpoint() { undo_stack.push_back({pieces, total}); redo_stack.clear(); }
```

Call `checkpoint()` once per user-visible change (entering insert mode, each
normal-mode command), not once per keystroke. After restoring a snapshot, rebuild the
line index.
