#pragma once
// Code folding: the model. No UI, no tree-sitter, no Buffer: it speaks line
// numbers only, so it can be tested on its own (see fold_test.cpp).
//
//   Ranges  what a provider reported, normalized into a nested tree.
//           Recomputed on reparse; never holds user state.
//   Closed  the lines the user closed. Kept apart from the ranges so it
//           survives recompute. An anchor whose range vanishes (a transient
//           parse error) stays "dormant" and hides nothing until the range is
//           back.
//   Spans   derived: the outermost closed spans plus prefix sums, which give
//           O(log n) line <-> visible-row conversion.
//
// A closed fold hides lines head+1..end; the head line stays visible.

#include <climits>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace fold {

// What a provider reports: an inclusive 0-based line range.
struct Raw {
    int start = 0, end = 0;
    bool operator==(const Raw&) const = default;
};

struct Range {
    int start = 0, end = 0;
    int level = 0;    // nesting depth, 0 = outermost
    int parent = -1;  // index into ranges(), -1 = none
};

struct Span {  // a closed fold as it is drawn
    int head = 0, end = 0;
};

// A text edit in line terms (cols matter only as "at column 0 or not").
struct Edit {
    int start_row = 0, start_col = 0;
    int old_end_row = 0;
    int new_end_row = 0;
};

// Provider: ordered list, first non-empty result wins (ufo style).
using Provider = std::function<std::vector<Raw>()>;
std::vector<Raw> run_providers(const std::vector<Provider>& ps);

// Fallback provider: fold by indentation. `indent_of(line)` is the indent
// width of a line, or -1 for a blank one.
std::vector<Raw> indent_folds(int nlines,
                              const std::function<int(int)>& indent_of);

// Sorts, dedupes same-start folds (outermost wins), trims a body that ends on
// its sibling's head line (`} else {`), drops folds that hide fewer than
// `min_lines` lines and clips partial overlaps.
std::vector<Raw> normalize(std::vector<Raw> raw, int min_lines);

class State {
public:
    // ---- ranges -----------------------------------------------------------
    // `level_start` >= 0 closes every fold at that depth or deeper the first
    // time ranges arrive (vim's foldlevelstart).
    void set_ranges(std::vector<Raw> raw, int min_lines, int level_start = -1);
    const std::vector<Range>& ranges() const { return ranges_; }
    int max_level() const;  // deepest level present, -1 when there are none
    void clear();           // buffer reset: forget everything
    // Drop what points past the end of the file.
    void clamp_lines(int total);

    // Ranges were shifted by an edit and not recomputed yet. The owner sets
    // and clears this; the model only reports it.
    bool stale = true;

    // ---- edits ------------------------------------------------------------
    void shift(const Edit& e);

    // ---- commands (return true when something changed) -------------------
    bool open(int line);        // zo
    bool close(int line);       // zc
    bool toggle(int line);      // za
    bool open_rec(int line);    // zO
    bool close_rec(int line);   // zC
    bool toggle_rec(int line);  // zA
    bool open_all();            // zR
    bool close_all();           // zM
    bool more(int n = 1);       // zm: close one more level everywhere
    bool fewer(int n = 1);      // zr: open one more level everywhere
    bool reveal(int line);      // zv / the foldopen choke point
    bool set_enabled(bool on);  // zn / zN / zi
    bool enabled() const { return enabled_; }
    // closes/opens every fold at depth >= lvl
    void apply_level(int lvl);

    // ---- navigation (visible lines, or nullopt) ---------------------------
    std::optional<int> next_fold_start(int line) const;  // zj
    std::optional<int> prev_fold_end(int line) const;    // zk
    std::optional<int> enclosing_start(int line) const;  // [z
    std::optional<int> enclosing_end(int line) const;    // ]z

    // ---- visibility -------------------------------------------------------
    bool any_closed() const { return !spans_.empty(); }
    const std::vector<Span>& spans() const { return spans_; }
    const Span* span_at(int line) const;  // closed span holding `line`
    bool hidden(int line) const;          // inside a span, not its head
    int head_of(int line) const;          // the line the cursor may rest on
    int end_of(int line) const;           // last line of the closed fold
    int next_visible(int line) const;     // first visible line after `line`
    int prev_visible(int line) const;     // -1 when there is none
    int hidden_total() const { return hidden_total_; }
    int hidden_in(const Span& s) const { return s.end - s.head; }
    int visible_row(int line) const;      // # of visible lines before it
    int line_at_row(int row) const;       // inverse; row may pass the end
    int visible_count(int total) const { return total - hidden_total_; }

    // ---- misc -------------------------------------------------------------
    bool is_anchor(int line) const;
    const std::vector<int>& anchors() const { return closed_; }
    uint64_t gen() const { return gen_; }  // bumps when visibility changes
    // innermost range holding `line` (index), -1 when none
    int innermost(int line) const;

private:
    void assign(std::vector<Raw> raw, int min_lines);
    void rebuild();
    bool closed(int idx) const { return is_anchor(ranges_[idx].start); }
    void add_anchor(int line);
    bool del_anchor(int line);
    std::vector<int> chain(int line) const;  // innermost -> outermost
    int span_idx(int line) const;            // last span with head <= line

    std::vector<Range> ranges_;
    std::vector<int> closed_;  // sorted anchors (head lines)
    std::vector<Span> spans_;
    std::vector<int> before_;  // hidden lines before spans_[i]
    int hidden_total_ = 0;
    int min_lines_ = 1;
    int level_ = INT_MAX;
    bool enabled_ = true;
    bool level_applied_ = false;
    uint64_t gen_ = 0;
};

}  // namespace fold
