// Build: g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined
//        -fno-sanitize-recover=undefined piece.cpp piece_test.cpp -o piece_test
#include "../src/piece.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#define CHECK(c)                                                              \
    do {                                                                      \
        if(!(c)) {                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            std::exit(1);                                                     \
        }                                                                     \
    } while(0)

static std::vector<size_t> model_line_starts(const std::string& s) {
    std::vector<size_t> v{0};
    for(size_t i = 0; i < s.size(); ++i)
        if(s[i] == '\n') v.push_back(i + 1);
    return v;
}

static void verify(const PieceTable& pt, const std::string& m,
                   std::mt19937& rng) {
    CHECK(pt.size() == m.size());
    CHECK(pt.text() == m);
    const auto starts = model_line_starts(m);
    CHECK(pt.line_count() == starts.size());
    for(size_t i = 0; i < starts.size(); ++i) {
        CHECK(pt.line_start(i) == starts[i]);
        const size_t end =
            (i + 1 < starts.size()) ? starts[i + 1] - 1 : m.size();
        CHECK(pt.line_end(i) == end);
        CHECK(pt.line_text(i) == m.substr(starts[i], end - starts[i]));
    }
    CHECK(pt.line_start(starts.size()) == PieceTable::npos);
    CHECK(pt.line_end(starts.size()) == PieceTable::npos);
    CHECK(pt.line_text(starts.size()).empty());

    for(int k = 0; k < 20; ++k) {
        const size_t pos = rng() % (m.size() + 1);
        const size_t n = rng() % 64;
        CHECK(pt.text(pos, n) == (pos >= m.size() ? "" : m.substr(pos, n)));
        if(pos < m.size()) CHECK(pt.at(pos) == m[pos]);
        const size_t line =
            std::upper_bound(starts.begin(), starts.end(), pos) -
            starts.begin() - 1;
        CHECK(pt.line_of(pos) == line);

        // line/col round trip
        const LineCol lc = pt.to_linecol(pos);
        CHECK(lc.line == line && lc.col == pos - starts[line]);
        CHECK(pt.from_linecol(lc.line, lc.col) == pos);

        // chunked read equals text(pos, n), and no chunk is empty
        std::string joined;
        pt.for_each_chunk(pos, n, [&](std::string_view c) {
            CHECK(!c.empty());
            joined.append(c);
        });
        CHECK(joined == pt.text(pos, n));
    }
    // col past end of line clamps to the line end; line past end clamps to last
    CHECK(pt.from_linecol(0, 100000) == pt.line_end(0));
    CHECK(pt.from_linecol(100000, 0) == starts.back());
    CHECK(pt.line_of(m.size()) == starts.size() - 1);
}

static std::string rand_text(std::mt19937& rng, size_t max_len) {
    static const char alpha[] = "abcdefgh\n\n  ";
    std::string s(rng() % (max_len + 1), ' ');
    for(char& c : s) c = alpha[rng() % (sizeof alpha - 1)];
    return s;
}

static bool do_edit(PieceTable& pt, std::string& m, std::mt19937& rng) {
    const uint64_t before = pt.revision();
    const size_t pos = rng() % (m.size() + 3);
    if(rng() % 3 != 0) {
        const std::string t = rand_text(rng, 12);
        pt.insert(pos, t);
        if(t.empty()) {
            CHECK(pt.revision() == before);
            return false;
        }
        m.insert(std::min(pos, m.size()), t);
    } else {
        const size_t n = rng() % 40;
        pt.remove(pos, n);
        if(pos >= m.size() || n == 0) {
            CHECK(pt.revision() == before);
            return false;
        }
        m.erase(pos, n);
    }
    CHECK(pt.revision() > before);
    return true;
}

// A run of backspaces or delete-forwards (exercises edit merging).
static bool do_delete_run(PieceTable& pt, std::string& m, std::mt19937& rng) {
    if(m.size() < 3) return false;
    bool eff = false;
    const bool backspace = rng() % 2;
    size_t pos = 1 + rng() % (m.size() - 1);
    const int k = 2 + static_cast<int>(rng() % 12);
    for(int j = 0; j < k && !m.empty(); ++j) {
        const size_t n = 1 + rng() % 3;
        if(backspace) {
            if(pos == 0) break;
            const size_t start = pos >= n ? pos - n : 0;
            const size_t len = pos - start;
            pt.remove(start, len);
            m.erase(start, len);
            pos = start;
        } else {
            if(pos >= m.size()) break;
            pt.remove(pos, n);
            m.erase(pos, std::min(n, m.size() - pos));
        }
        eff = true;
    }
    return eff;
}

struct Model {
    std::vector<std::string> st;
    std::vector<int> ids;
    size_t i = 0;
    int next_id = 1;
    int saved_id = 0;
    size_t limit = 0;  // history limit (revisions), 0 = unlimited

    const std::string& cur() const { return st[i]; }
    bool modified() const { return ids[i] != saved_id; }
    void commit(std::string s) {
        st.resize(i + 1);
        ids.resize(i + 1);
        st.push_back(std::move(s));
        ids.push_back(next_id++);
        ++i;
        if(limit && st.size() - 1 > limit) {
            const size_t d = st.size() - 1 - limit;
            st.erase(st.begin(), st.begin() + static_cast<std::ptrdiff_t>(d));
            ids.erase(ids.begin(),
                      ids.begin() + static_cast<std::ptrdiff_t>(d));
            i -= d;
        }
    }
};

int main() {
    // empty table, no history
    {
        PieceTable pt;
        CHECK(pt.size() == 0 && pt.line_count() == 1);
        CHECK(!pt.undo() && !pt.redo() && !pt.can_undo() && !pt.can_redo());
        CHECK(!pt.modified());
        pt.remove(0, 10);
        CHECK(!pt.can_undo() && !pt.modified());
        pt.insert(100, "hi");
        CHECK(pt.text() == "hi" && pt.modified());
        CHECK(*pt.undo() == 0 && pt.text().empty() && !pt.modified());
        CHECK(*pt.redo() == 0 && pt.text() == "hi");
        CHECK(pt.line_text(0) == "hi" && pt.line_end(0) == 2);
    }

    // typing run in a group: one node, one undo step
    {
        PieceTable pt("hello");
        {
            PieceTable::Group g(pt);
            for(int i = 0; i < 500; ++i) pt.insert(pt.size(), "x");
        }
        CHECK(pt.size() == 505 && pt.piece_count() == 2);
        CHECK(*pt.undo() == 5 && pt.text() == "hello");
        CHECK(!pt.undo());
        CHECK(pt.redo() && pt.size() == 505);
    }

    // backspace run and delete-forward run each undo in one step
    {
        PieceTable pt("0123456789abcdef");
        {
            PieceTable::Group g(pt);
            for(size_t p = 8; p > 2; --p) pt.remove(p - 1, 1);  // backspaces
        }
        CHECK(pt.text() == "0189abcdef");
        {
            PieceTable::Group g(pt);
            for(int i = 0; i < 4; ++i) pt.remove(3, 1);  // delete-forward
        }
        CHECK(pt.text() == "018def");
        CHECK(*pt.undo() == 3 && pt.text() == "0189abcdef");
        CHECK(*pt.undo() == 2 && pt.text() == "0123456789abcdef");
        CHECK(!pt.undo());
        CHECK(pt.redo() && pt.redo() && pt.text() == "018def");
    }

    // empty group neither adds a step nor clears redo
    {
        PieceTable pt("abc");
        pt.insert(3, "d");
        pt.undo();
        { PieceTable::Group g(pt); }
        CHECK(pt.can_redo());
        CHECK(pt.redo() && pt.text() == "abcd");
    }

    // new edit after undo discards redo
    {
        PieceTable pt("abc");
        pt.insert(3, "d");
        pt.undo();
        pt.insert(0, "Z");
        CHECK(!pt.can_redo() && pt.text() == "Zabc");
    }

    // arena doesn't grow unboundedly under churn
    {
        PieceTable pt("0123456789");
        for(int i = 0; i < 20000; ++i) {
            pt.insert(5, "xyz");
            pt.remove(5, 3);
        }
        CHECK(pt.text() == "0123456789");
        CHECK(pt.arena_size() < 100);
    }

    // dirty tracking
    {
        PieceTable pt("abc");
        CHECK(!pt.modified());
        pt.insert(3, "d");
        CHECK(pt.modified());
        pt.undo();
        CHECK(!pt.modified());  // back at the loaded text
        pt.redo();
        CHECK(pt.modified());
        pt.mark_saved();
        CHECK(!pt.modified());
        pt.undo();
        CHECK(pt.modified());
        pt.redo();
        CHECK(!pt.modified());  // back at the saved text

        // saved state discarded from the redo stack -> stays modified
        pt.undo();          // text "abc", saved state "abcd" is redo
        pt.insert(0, "Z");  // discards redo (and the saved state)
        CHECK(pt.modified());
        pt.undo();
        CHECK(pt.text() == "abc" && pt.modified());  // "abc" was never saved

        // open group counts as modified
        PieceTable p2("x");
        p2.mark_saved();
        p2.begin_group();
        p2.insert(1, "y");
        CHECK(p2.modified());
        p2.end_group();
        CHECK(p2.modified());
        p2.undo();
        CHECK(!p2.modified());
    }

    // history limit
    {
        PieceTable pt("");
        pt.set_history_limit(5);
        for(int i = 0; i < 20; ++i)
            pt.insert(pt.size(), std::string(1, char('a' + i)));
        CHECK(pt.text() == "abcdefghijklmnopqrst");
        int undone = 0;
        while(pt.undo()) ++undone;
        CHECK(undone == 5 && pt.text() == "abcdefghijklmno");
        CHECK(!pt.can_undo() && pt.can_redo());
        for(int i = 0; i < 5; ++i) CHECK(pt.redo());
        CHECK(pt.text() == "abcdefghijklmnopqrst");

        // saved state older than the retained history -> modified for good
        PieceTable p2("");
        p2.set_history_limit(3);
        p2.insert(0, "a");
        p2.mark_saved();
        for(int i = 0; i < 6; ++i) p2.insert(p2.size(), "b");
        while(p2.undo()) {}
        CHECK(p2.modified());

        // lowering the limit later trims immediately
        PieceTable p3("");
        for(int i = 0; i < 10; ++i) p3.insert(p3.size(), "x");
        p3.set_history_limit(2);
        int u = 0;
        while(p3.undo()) ++u;
        CHECK(u == 2 && p3.size() == 8);
    }

    // randomized differential run: groups, delete runs, undo, redo,
    // dirty tracking, history limit, line helpers, chunks
    for(unsigned seed = 1; seed <= 60; ++seed) {
        std::mt19937 rng(seed);
        const std::string orig = rand_text(rng, 300);
        PieceTable pt{orig};
        Model ref;
        ref.st.push_back(orig);
        ref.ids.push_back(0);
        if(seed % 3 == 0) {
            ref.limit = 4 + seed % 20;
            pt.set_history_limit(ref.limit);
        }

        for(int op = 0; op < 2500; ++op) {
            const unsigned r = rng() % 100;
            if(r < 38) {
                std::string work = ref.cur();
                if(do_edit(pt, work, rng)) ref.commit(work);
            } else if(r < 58) {
                std::string work = ref.cur();
                bool eff = false;
                {
                    PieceTable::Group g(pt);
                    const int k = 1 + static_cast<int>(rng() % 6);
                    for(int j = 0; j < k; ++j) {
                        if(rng() % 4 == 0) {
                            PieceTable::Group g2(pt);
                            eff |= do_edit(pt, work, rng);
                        } else {
                            eff |= do_edit(pt, work, rng);
                        }
                    }
                    CHECK(!pt.undo() && !pt.redo());
                    if(eff) CHECK(pt.modified());
                }
                if(eff) ref.commit(work);
            } else if(r < 68) {  // delete run in one group
                std::string work = ref.cur();
                bool eff;
                {
                    PieceTable::Group g(pt);
                    eff = do_delete_run(pt, work, rng);
                }
                if(eff) ref.commit(work);
            } else if(r < 79) {  // undo
                const bool can = ref.i > 0;
                const uint64_t before = pt.revision();
                const auto at = pt.undo();
                CHECK(at.has_value() == can);
                if(can) {
                    --ref.i;
                    CHECK(*at <= pt.size());
                    CHECK(pt.revision() > before);
                }
            } else if(r < 90) {  // redo
                const bool can = ref.i + 1 < ref.st.size();
                const uint64_t before = pt.revision();
                const auto at = pt.redo();
                CHECK(at.has_value() == can);
                if(can) {
                    ++ref.i;
                    CHECK(*at <= pt.size());
                    CHECK(pt.revision() > before);
                }
            } else if(r < 94) {  // save
                pt.mark_saved();
                ref.saved_id = ref.ids[ref.i];
            }
            CHECK(pt.text() == ref.cur());
            CHECK(pt.can_undo() == (ref.i > 0));
            CHECK(pt.can_redo() == (ref.i + 1 < ref.st.size()));
            CHECK(pt.modified() == ref.modified());
            if(op % 40 == 0) verify(pt, ref.cur(), rng);
        }
        verify(pt, ref.cur(), rng);
    }

    std::puts("ALL PASS");
    return 0;
}
