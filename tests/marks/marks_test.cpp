#include "../src/marks.hpp"
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include "../src/piece.hpp"

static std::map<uint64_t, std::unique_ptr<PieceTable>> g_t;
static std::map<uint64_t, std::string> g_f;
static void host() {
    marks_set_host({[](uint64_t id) -> PieceTable* {
                        auto it = g_t.find(id);
                        return it == g_t.end() ? nullptr : it->second.get();
                    },
                    [](uint64_t id) { return g_f[id]; }});
}
static int n = 0, bad = 0;
#define CHECK(c)                                             \
    do {                                                     \
        ++n;                                                 \
        if(!(c)) {                                           \
            ++bad;                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
        }                                                    \
    } while(0)

int main() {
    host();
    g_t[1] = std::make_unique<PieceTable>("alpha\nbeta\ngamma\n");
    g_f[1] = "one.txt";
    g_t[2] = std::make_unique<PieceTable>("xx\nyy\n");
    g_f[2] = "two.txt";
    PieceTable& a = *g_t[1];

    // set + find: line/col/offset
    CHECK(mark_put('a', 1, 8));  // "beta": col 2 of line 1
    auto m = mark_find('a', 1);
    CHECK(m && m->buf == 1 && m->line == 1 && m->col == 2 && m->off == 8 &&
          m->file == "one.txt");
    // follows edits: insert before it
    a.insert(0, "NEW\n");
    m = mark_find('a', 1);
    CHECK(m && m->line == 2 && m->col == 2 && m->off == 12);
    // text typed exactly at the mark pushes it along with its character
    a.insert(12, "__");
    m = mark_find('a', 1);
    CHECK(m && m->off == 14 && m->col == 4);
    // an insert after it leaves it
    a.insert(a.size(), "tail");
    m = mark_find('a', 1);
    CHECK(m && m->off == 14);
    // deleting a range that contains it slides it to the start of the deletion
    a.remove(10, 8);
    m = mark_find('a', 1);
    CHECK(m && m->off == 10);
    // deleting before it moves it back
    a.remove(0, 4);
    m = mark_find('a', 1);
    CHECK(m && m->off == 6);

    // re-setting replaces (and does not leak the old anchor into the table)
    CHECK(mark_put('a', 1, 0));
    m = mark_find('a', 1);
    CHECK(m && m->off == 0);

    // lowercase marks are per buffer
    CHECK(!mark_find('a', 2).has_value());
    CHECK(mark_put('a', 2, 3));
    CHECK(mark_find('a', 2)->off == 3 && mark_find('a', 1)->off == 0);

    // validation
    CHECK(!mark_put('1', 1, 0) && !mark_put('.', 1, 0) &&
          !mark_put('?', 1, 0) && !mark_put(' ', 1, 0));
    CHECK(!mark_put('b', 99, 0));  // no such buffer
    CHECK(mark_name_settable('z') && mark_name_settable('Z') &&
          mark_name_settable('`') && !mark_name_settable('.') &&
          mark_name_valid('.'));

    // file marks: global, visible from other buffers, survive closing
    CHECK(mark_put('A', 1, 2));
    auto g = mark_find('A', 2);
    CHECK(g && g->buf == 1 && g->file == "one.txt" && g->off == 2);
    a.insert(0, "zz");  // still tracks while live
    CHECK(mark_find('A', 2)->off == 4);
    CHECK(mark_put('B', 2, 1));
    CHECK(mark_find('B', 1)->buf == 2 && mark_find('B', 1)->file == "two.txt");
    mark_buffer_closing(1);
    CHECK(
        !mark_find('a', 1).has_value());  // dropped while the text still exists
    g_t.erase(1);  // the editor drops the buffer after announcing the close
    g = mark_find('A', 2);
    CHECK(g && g->buf == 0 && g->file == "one.txt" && g->line == 0 &&
          g->col == 4);                     // frozen where it was
    CHECK(!mark_find('a', 1).has_value());  // buffer-local ones are gone
    CHECK(mark_find('B', 2)->buf == 2);     // other buffers unaffected
    // moving a file mark to another buffer leaves no stale anchor behind
    CHECK(mark_put('B', 2, 4) && mark_find('B', 2)->off == 4);

    // '' : set before a jump, found from anywhere, '`' is the same mark
    CHECK(!mark_find('\'', 2).has_value());
    mark_prev(2, 3);
    CHECK(mark_find('\'', 2)->off == 3 && mark_find('`', 2)->off == 3);
    mark_prev(2, 1);
    CHECK(mark_find('\'', 2)->off == 1);
    CHECK(mark_put('`', 2, 5) && mark_find('\'', 2)->off == 5);

    // '.' and '<' '>' come from the editor
    CHECK(!mark_find('.', 2).has_value());
    mark_changed(2, 2);
    CHECK(mark_find('.', 2)->off == 2);
    mark_changed(2, 4);
    CHECK(mark_find('.', 2)->off == 4);
    mark_visual(2, 1, 4);
    CHECK(mark_find('<', 2)->off == 1 && mark_find('>', 2)->off == 4);

    // list
    std::string names;
    for(char c : mark_names(2)) names += c;
    CHECK(names == "a.<>AB'");
    // delete
    CHECK(mark_del('a', 2) && !mark_find('a', 2).has_value() &&
          !mark_del('a', 2));
    CHECK(mark_del('B', 2) && !mark_find('B', 2).has_value());
    CHECK(mark_del('\'', 2) && !mark_find('\'', 2).has_value());

    // closing a buffer clears '' that pointed into it
    mark_prev(2, 0);
    mark_buffer_closing(2);
    CHECK(!mark_find('\'', 2).has_value());

    // mark_put_at: line/col clamped into the text
    g_t[3] = std::make_unique<PieceTable>("one\ntwo2\nthree\n");
    g_f[3] = "three.txt";
    CHECK(mark_put_at('q', 3, 1, 2) && mark_find('q', 3)->off == 6 &&
          mark_find('q', 3)->line == 1 && mark_find('q', 3)->col == 2);
    CHECK(mark_put_at('q', 3, 1, 99) && mark_find('q', 3)->off == 8 &&
          mark_find('q', 3)->col == 4);  // col clamps to the line end
    CHECK(mark_put_at('q', 3, 99, 0) &&
          mark_find('q', 3)->line ==
              3);  // line clamps to the last line (empty last line)
    CHECK(!mark_put_at('q', 99, 0, 0) && !mark_put_at('1', 3, 0, 0));
    std::printf("%d checks, %d failed\n", n, bad);
    return bad != 0;
}
