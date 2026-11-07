// g++ -std=c++23 -Wall -Wextra fold.cpp fold_test.cpp -o fold_test &&
// ./fold_test
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/fold.hpp"

using namespace fold;
static int fails = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if(!(c)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++fails;                                                 \
        }                                                            \
    } while(0)

// brute-force oracle for visible_row / line_at_row
static void oracle(const State& s, int total) {
    std::vector<int> vis;
    for(int l = 0; l < total; ++l)
        if(!s.hidden(l)) vis.push_back(l);
    CHECK((int)vis.size() == s.visible_count(total));
    for(int r = 0; r < (int)vis.size(); ++r) {
        CHECK(s.line_at_row(r) == vis[r]);
        CHECK(s.visible_row(vis[r]) == r);
    }
    for(int l = 0; l < total; ++l)
        CHECK(s.visible_row(l) == s.visible_row(s.head_of(l)));
    CHECK(s.line_at_row((int)vis.size()) == total);  // one past the end
}

int main() {
    // 0 fn() {            fold A 0-9
    // 1   if(a) {         fold B 1-3   (closer line 3 is `} else if(b) {`)
    // 2     x;
    // 3   } else if(b) {  fold C 3-6
    // 4     y;
    // 5     for(..) {     fold D 5-6 (closer 7)
    // 6       z;
    // 7     }
    // 8   }
    // 9 }
    {
        auto n = normalize({{1, 3}, {3, 8}, {0, 9}, {5, 7}, {2, 2}, {4, 4}}, 1);
        // B {1,3} ends on C's head -> trimmed to 1-2; D {5,7}
        CHECK((n == std::vector<Raw>{{0, 9}, {1, 2}, {3, 8}, {5, 7}}));
        // same-start: outermost wins; partial overlap clipped
        n = normalize({{2, 5}, {2, 9}, {4, 12}}, 1);
        CHECK((n == std::vector<Raw>{{2, 9}, {4, 9}}));
        // minlines
        n = normalize({{0, 1}, {3, 6}}, 2);
        CHECK((n == std::vector<Raw>{{3, 6}}));
    }
    {
        std::vector<int> ind = {0, 4, 8, 4, 8, 8, 12, 8, 4, 0};
        auto f = indent_folds((int)ind.size(), [&](int l) { return ind[l]; });
        auto n = normalize(f, 1);
        // fn 0-8, if 1-2 (closer line 3 at same indent starts else-if), else-if
        // 3-7, for 5-6
        CHECK((n == std::vector<Raw>{{0, 8}, {1, 2}, {3, 7}, {5, 6}}));
        // blank lines are skipped
        std::vector<int> i2 = {0, 4, -1, 4, 0};
        auto f2 = indent_folds(5, [&](int l) { return i2[l]; });
        CHECK((f2 == std::vector<Raw>{{0, 3}}));
    }

    State s;
    s.set_ranges({{0, 9}, {1, 2}, {3, 8}, {5, 7}}, 1);
    CHECK(s.ranges().size() == 4);
    CHECK(s.ranges()[3].level == 2 && s.ranges()[3].parent == 2);
    CHECK(s.innermost(6) == 3 && s.innermost(4) == 2 && s.innermost(9) == 0);

    // zc twice: inner then parent
    CHECK(s.close(6));  // closes D (5-7)
    CHECK(s.hidden(6) && !s.hidden(5));
    CHECK(s.head_of(7) == 5 && s.end_of(5) == 7);
    oracle(s, 10);
    CHECK(s.close(5));  // cursor on closed D: closes parent C (3-8)
    CHECK(s.span_at(4) && s.span_at(4)->head == 3);
    oracle(s, 10);
    CHECK(s.open(3));  // zo opens exactly one level; D stays closed
    CHECK(!s.hidden(4) && s.hidden(6));
    CHECK(s.is_anchor(5));
    oracle(s, 10);
    CHECK(s.close_all());
    CHECK(s.spans().size() == 1 &&
          s.hidden_total() == 9);  // only the outermost draws
    oracle(s, 10);
    CHECK(
        s.fewer(1));  // zr: level 1 -> A open, B C closed, D (inside C) hidden
    CHECK(s.spans().size() == 2 && s.head_of(2) == 1);
    oracle(s, 10);
    CHECK(s.more(1));  // zm back to everything closed
    CHECK(s.spans().size() == 1);
    CHECK(s.open_all() && !s.any_closed());
    oracle(s, 10);

    // dormant anchor: range disappears (parse error), then returns
    CHECK(s.close(6));
    s.set_ranges({{0, 9}, {1, 2}}, 1);
    CHECK(!s.any_closed() && s.is_anchor(5));
    s.set_ranges({{0, 9}, {1, 2}, {3, 8}, {5, 7}}, 1);
    CHECK(s.hidden(6));
    s.open_all();

    // reveal (foldopen choke point)
    s.close_all();
    CHECK(s.reveal(6));
    CHECK(s.spans().size() == 1 &&
          s.spans()[0].head == 1);  // only the folds holding line 6 open

    // navigation
    s.open_all();
    CHECK(s.next_fold_start(0) == 1);
    CHECK(s.next_fold_start(1) == 3);
    CHECK(s.next_fold_start(5) == std::nullopt);
    CHECK(s.prev_fold_end(5) == 2);  // end of B
    CHECK(s.enclosing_start(6) == 5);
    CHECK(s.enclosing_start(5) == 3);
    CHECK(s.enclosing_end(6) == 7);
    CHECK(s.enclosing_end(7) == 8);
    s.close(5);
    CHECK(s.enclosing_end(5) ==
          8);  // closed D's own end is hidden: next one out

    // ---- edit shifting ----
    s.open_all();
    s.close(6);
    s.close(1);  // D (5-7) and B (1-2) closed
    // `O` on D's head: "\n" inserted at (5,0): rows 5 -> 6
    s.shift({5, 0, 5, 6});
    CHECK(s.is_anchor(6) && !s.is_anchor(5));
    CHECK(s.ranges().back().start == 6 && s.ranges().back().end == 8);
    CHECK(s.stale);
    // Enter in the middle of a line (col > 0) keeps the line, shifts what
    // follows
    s.shift({0, 3, 0, 1});
    CHECK(s.is_anchor(7) && s.is_anchor(1 + 1));
    // undo it
    s.shift({0, 3, 1, 0});
    CHECK(s.is_anchor(6) && s.is_anchor(1));
    // dd on B's body line 2: bytes (2,0)-(3,0)
    s.shift({2, 0, 3, 2});
    CHECK(s.is_anchor(1));
    CHECK(s.ranges().size() == 3);  // B shrank to a single line and was dropped
    // delete the lines holding D's head: anchor goes, range goes
    s.shift({5, 0, 7, 5});
    CHECK(!s.is_anchor(5) && !s.is_anchor(4));
    // edit inside one line: nothing moves
    auto before = s.anchors();
    s.shift({3, 2, 3, 3});
    CHECK(before == s.anchors());

    // clamp
    s.set_ranges({{0, 9}, {3, 8}}, 1);
    s.open_all();
    s.close_all();
    s.clamp_lines(6);
    CHECK(s.ranges().size() == 2 && s.ranges()[0].end == 5 &&
          s.ranges()[1].end == 5);

    // randomized oracle: spans from random nested closed sets
    srand(7);
    for(int it = 0; it < 300; ++it) {
        std::vector<Raw> raw;
        const int total = 40 + rand() % 40;
        for(int k = 0; k < 14; ++k) {
            int a = rand() % total, b = a + 1 + rand() % 12;
            raw.push_back({a, std::min(b, total - 1)});
        }
        State t;
        t.set_ranges(raw, 1);
        for(int k = 0; k < 6; ++k) t.close(rand() % total);
        if(rand() % 3 == 0) t.more(1);
        oracle(t, total);
        // every closed span is a real range with its anchor
        for(const Span& sp : t.spans()) CHECK(t.is_anchor(sp.head));
    }
    std::printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
