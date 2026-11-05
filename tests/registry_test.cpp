#include <cstdio>
#include <string>

#include "../src/keys.hpp"
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
        auto fn = [](const PieceTable&, size_t p, int, std::string_view) {
            return Target{p};
        };
        r.add_motion("m.same", fn);
        CHECK(r.motion("m.same") != nullptr);
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

        Hit h = r.lookup(NORMAL, U"w");
        CHECK(h.kind == Lookup::Exact && h.action == "word" && !h.longer);
        CHECK(r.lookup(INSERT, U"w").action == "insert_w");  // modes are separate
        CHECK(r.lookup(NORMAL, U"z").kind == Lookup::None);
        CHECK(r.lookup(VISUAL, U"w").kind == Lookup::None);  // mode with no map
        CHECK(r.lookup(NORMAL, U"").kind == Lookup::None);

        h = r.lookup(NORMAL, U"g");
        CHECK(h.kind == Lookup::Exact && h.longer);         // wait for more keys
        h = r.lookup(NORMAL, U"gg");
        CHECK(h.kind == Lookup::Exact && !h.longer && h.action == "file_start");
        CHECK(r.lookup(NORMAL, U"gx").kind == Lookup::None);

        r.map(NORMAL, "<space>f", "find_file");
        r.map(NORMAL, " p", "paste");
        h = r.lookup(NORMAL, U" ");
        CHECK(h.kind == Lookup::Prefix && h.longer);
        CHECK(r.lookup(NORMAL, U" f").action == "find_file");

        CHECK(r.unmap(NORMAL, U"gg"));
        CHECK(!r.unmap(NORMAL, U"gg"));
        h = r.lookup(NORMAL, U"g");
        CHECK(h.kind == Lookup::Exact && !h.longer);        // no longer a prefix
        r.map(NORMAL, "w", "other");                        // rebind replaces
        CHECK(r.lookup(NORMAL, U"w").action == "other");
    }
    // ---- key notation
    CHECK(parse_keys("gg") == U"gg");
    CHECK(parse_keys("<C-r>") == U"\x12");
    CHECK(parse_keys("<c-R>") == U"\x12");
    CHECK(parse_keys("<Esc>") == U"\x1b");
    CHECK(parse_keys("<CR><Tab><BS>") == U"\r\t\x7f");
    CHECK(parse_keys("<space>f") == U" f");
    CHECK(parse_keys("a<lt>b") == U"a<b");
    CHECK(parse_keys("<bogus>") == U"<bogus>");   // unknown name stays literal
    CHECK(parse_keys("a<b") == U"a<b");           // lone '<'
    CHECK(parse_keys("<C-") == U"<C-");           // unterminated

    // ---- modified space keys (insert-mode symbol chords)
    CHECK(parse_keys("<Space>") == KeySeq{U' '});
    CHECK((parse_keys("<S-Space>") ==
           KeySeq{static_cast<char32_t>(U' ' | MOD_SHIFT)}));
    CHECK((parse_keys("<C-Space>") == KeySeq{0}));
    CHECK(parse_keys("<S-Space>") != parse_keys("<Space>"));
    CHECK(format_keys(parse_keys("<S-Space>")) == "<S-Space>");
    CHECK(format_keys(parse_keys("<C-Space>")) == "<C-Space>");
    CHECK((parse_keys("<C-Space>a") == KeySeq{0, U'a'}));

    // ---- modified Enter keys
    CHECK((parse_keys("<S-CR>") ==
           KeySeq{static_cast<char32_t>('\r' | MOD_SHIFT)}));
    CHECK((parse_keys("<C-CR>") ==
           KeySeq{static_cast<char32_t>('\r' | MOD_CTRL)}));
    CHECK((parse_keys("<A-CR>") ==
           KeySeq{static_cast<char32_t>('\r' | MOD_ALT)}));
    CHECK(parse_keys("<S-CR>") != parse_keys("<CR>"));
    CHECK(format_keys(parse_keys("<S-CR>")) == "<S-CR>");
    CHECK(format_keys(parse_keys("<C-CR>")) == "<C-CR>");
    CHECK(format_keys(parse_keys("<A-CR>")) == "<A-CR>");

    {
        Registry r;
        r.map(INSERT, "<S-Space>a", "insert.alpha");
        r.map(INSERT, "<C-Space>b", "insert.beta");
        r.map(REPLACE, "<S-Space>a", "replace.alpha");
        r.map(COMMAND, "<S-Space>a", "command.alpha");
        r.map(SEARCH, "<C-Space>b", "search.beta");
        r.map(INSERT, "<S-CR>", "insert.enter");
        r.map(REPLACE, "<C-CR>", "replace.enter");
        r.map(COMMAND, "<A-CR>", "command.enter");
        r.map(SEARCH, "<S-CR>", "search.enter");

        CHECK(r.lookup(INSERT, parse_keys("<S-Space>a")).action ==
              "insert.alpha");
        CHECK(r.lookup(INSERT, parse_keys("<C-Space>b")).action ==
              "insert.beta");
        CHECK(r.lookup(REPLACE, parse_keys("<S-Space>a")).action ==
              "replace.alpha");
        CHECK(r.lookup(COMMAND, parse_keys("<S-Space>a")).action ==
              "command.alpha");
        CHECK(r.lookup(SEARCH, parse_keys("<C-Space>b")).action ==
              "search.beta");
        CHECK(r.lookup(INSERT, parse_keys("<S-CR>")).action ==
              "insert.enter");
        CHECK(r.lookup(REPLACE, parse_keys("<C-CR>")).action ==
              "replace.enter");
        CHECK(r.lookup(COMMAND, parse_keys("<A-CR>")).action ==
              "command.enter");
        CHECK(r.lookup(SEARCH, parse_keys("<S-CR>")).action ==
              "search.enter");
        // <Space> is a distinct key from <S-Space>, <CR> from <S-CR>
        CHECK(r.lookup(INSERT, parse_keys("<Space>a")).kind == Lookup::None);
        CHECK(r.lookup(INSERT, parse_keys("<CR>")).kind == Lookup::None);
    }

    std::printf(failed ? "%d FAILED\n" : "registry: all passed\n", failed);
    return failed != 0;
}
