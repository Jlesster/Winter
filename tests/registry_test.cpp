#include <cstdio>
#include <string>

#include "../src/registry.hpp"

static int failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if(!(cond)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failed;                                                    \
        }                                                                \
    } while(0)

int main() {
    // ---- actions
    {
        Registry r;
        int hits = 0, count_seen = -1;
        std::string arg_seen;
        r.add_action("a.one", [&](Ctx& c) { ++hits; count_seen = c.count; arg_seen = c.arg; });
        r.add_action("a.char", [](Ctx&) {}, /*wants_char=*/true);

        Ctx ctx{3, "x"};
        CHECK(r.run_action("a.one", ctx));
        CHECK(hits == 1 && count_seen == 3 && arg_seen == "x");
        CHECK(!r.run_action("nope", ctx));
        CHECK(!r.wants_char("a.one"));
        CHECK(r.wants_char("a.char"));
        CHECK(!r.wants_char("nope"));
        CHECK(r.action_names().size() == 2);

        // re-adding a name replaces it: this is how Lua will override builtins
        r.add_action("a.one", [&](Ctx&) { hits += 100; });
        r.run_action("a.one", ctx);
        CHECK(hits == 101);
    }
    // ---- motions
    {
        Registry r;
        auto fn = [](const PieceTable&, size_t p, int) { return Target{p}; };
        r.add_motion("m.same", fn);
        CHECK(r.motion("m.same") == fn);
        CHECK(r.motion("m.none") == nullptr);
    }
    // ---- keymaps
    {
        Registry r;
        r.map(NORMAL, "g", "never");     // exact AND a prefix of "gg"
        r.map(NORMAL, "gg", "file_start");
        r.map(NORMAL, "G", "file_end");
        r.map(NORMAL, "w", "word");
        r.map(INSERT, "w", "insert_w");

        Hit h = r.lookup(NORMAL, "w");
        CHECK(h.kind == Lookup::Exact && h.action == "word" && !h.longer);
        CHECK(r.lookup(INSERT, "w").action == "insert_w");  // modes are separate
        CHECK(r.lookup(NORMAL, "z").kind == Lookup::None);
        CHECK(r.lookup(VISUAL, "w").kind == Lookup::None);  // mode with no map
        CHECK(r.lookup(NORMAL, "").kind == Lookup::None);

        h = r.lookup(NORMAL, "g");
        CHECK(h.kind == Lookup::Exact && h.longer);         // wait for more keys
        h = r.lookup(NORMAL, "gg");
        CHECK(h.kind == Lookup::Exact && !h.longer && h.action == "file_start");
        CHECK(r.lookup(NORMAL, "gx").kind == Lookup::None);

        r.map(NORMAL, "<space>f", "find_file");
        r.map(NORMAL, " p", "paste");
        h = r.lookup(NORMAL, " ");
        CHECK(h.kind == Lookup::Prefix && h.longer);
        CHECK(r.lookup(NORMAL, " f").kind == Lookup::None);  // literal "<space>f" isn't " f"

        CHECK(r.unmap(NORMAL, "gg"));
        CHECK(!r.unmap(NORMAL, "gg"));
        h = r.lookup(NORMAL, "g");
        CHECK(h.kind == Lookup::Exact && !h.longer);        // no longer a prefix
        r.map(NORMAL, "w", "other");                        // rebind replaces
        CHECK(r.lookup(NORMAL, "w").action == "other");
    }
    // ---- key notation
    CHECK(parse_keys("gg") == "gg");
    CHECK(parse_keys("<C-r>") == "\x12");
    CHECK(parse_keys("<c-R>") == "\x12");
    CHECK(parse_keys("<Esc>") == "\x1b");
    CHECK(parse_keys("<CR><Tab><BS>") == "\r\t\x7f");
    CHECK(parse_keys("<space>f") == " f");
    CHECK(parse_keys("a<lt>b") == "a<b");
    CHECK(parse_keys("<bogus>") == "<bogus>");   // unknown name stays literal
    CHECK(parse_keys("a<b") == "a<b");           // lone '<'
    CHECK(parse_keys("<C-") == "<C-");           // unterminated

    std::printf(failed ? "%d FAILED\n" : "registry: all passed\n", failed);
    return failed != 0;
}
