#include <cstdio>
#include <string>

#include "../src/chord.hpp"
#include "../src/keys.hpp"

static int failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if(!(cond)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failed;                                                    \
        }                                                                \
    } while(0)

int main() {
    // Terminal normalization / fallback text
    CHECK(key_normalize(' ', MOD_SHIFT) == (' ' | MOD_SHIFT));
    CHECK(key_normalize(' ', MOD_CTRL) == 0);
    CHECK(key_text(' ' | MOD_SHIFT) == ' ');
    CHECK(key_text(0) == NONE);
    CHECK(key_text('a') == 'a');
    CHECK(key_text(ARROW_LEFT | MOD_CTRL) == ARROW_LEFT);

    // Modified Enter stays distinct for mappings but falls back to Enter
    CHECK(key_normalize('\r', MOD_SHIFT) == ('\r' | MOD_SHIFT));
    CHECK(key_normalize('\r', MOD_CTRL) == ('\r' | MOD_CTRL));
    CHECK(key_normalize('\r', MOD_ALT) == ('\r' | MOD_ALT));
    CHECK(key_text('\r' | MOD_SHIFT) == '\r');
    CHECK(key_text('\r' | MOD_CTRL) == '\r');
    CHECK(key_text('\r' | MOD_ALT) == '\r');

    Registry& r = registry();
    int hits = 0;
    std::string arg_seen;

    r.add_action("test.hit", [&](Ctx&) { ++hits; });
    r.add_action(
        "test.char", [&](Ctx& c) { arg_seen = c.arg; }, /*wants_char=*/true);
    r.add_action("test.space", [&](Ctx&) { ++hits; });

    r.map(INSERT, "<S-Space>a", "test.hit");
    r.map(INSERT, "<C-Space>b", "test.hit");
    r.map(INSERT, "<S-Space>f", "test.char");
    r.map(INSERT, "<Space>", "test.space");
    r.map(INSERT, "<Space>z", "test.hit");
    r.map(INSERT, "<S-CR>a", "test.hit");
    r.map(INSERT, "<C-CR>b", "test.hit");

    Chord chord;
    std::string emitted;
    const auto emit = [&](int key) {
        const int c = key_text(key);
        if(c != NONE && c < 256) emitted += static_cast<char>(c);
    };

    // prefix, then completion
    CHECK(chord.feed(INSERT, ' ' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.pending());
    CHECK(chord.feed(INSERT, 'a', emit) == Chord::Result::Handled);
    CHECK(!chord.pending());
    CHECK(hits == 1);
    CHECK(emitted.empty());

    // Ctrl-Space normalizes to NUL and is mappable
    hits = 0;
    CHECK(chord.feed(INSERT, 0, emit) == Chord::Result::Buffered);
    CHECK(chord.feed(INSERT, 'b', emit) == Chord::Result::Handled);
    CHECK(hits == 1);

    // prefix, then mismatch: the pending S-Space is replayed as a space
    hits = 0;
    emitted.clear();
    CHECK(chord.feed(INSERT, ' ' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.feed(INSERT, 'q', emit) == Chord::Result::Pass);
    CHECK(emitted == " ");
    CHECK(hits == 0);

    // timeout of a prefix-only chord replays the key as literal text
    emitted.clear();
    CHECK(chord.feed(INSERT, ' ' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    chord.timeout(emit);
    CHECK(emitted == " ");
    CHECK(!chord.pending());

    // timeout resolves an exact mapping that is also a longer prefix
    hits = 0;
    CHECK(chord.feed(INSERT, ' ', emit) == Chord::Result::Buffered);
    chord.timeout(emit);
    CHECK(hits == 1);
    CHECK(!chord.pending());

    // wants_char: the key after the mapping becomes the action argument
    arg_seen.clear();
    CHECK(chord.feed(INSERT, ' ' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.feed(INSERT, 'f', emit) == Chord::Result::Handled);
    CHECK(!chord.pending());
    CHECK(chord.feed(INSERT, 'x', emit) == Chord::Result::Handled);
    CHECK(arg_seen == "x");

    // Esc cancels a pending chord instead of replaying it
    emitted.clear();
    CHECK(chord.feed(INSERT, ' ' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.feed(INSERT, '\x1b', emit) == Chord::Result::Pass);
    CHECK(!chord.pending());
    CHECK(emitted.empty());

    // modified Enter completes a chord
    hits = 0;
    emitted.clear();
    CHECK(chord.feed(INSERT, '\r' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.feed(INSERT, 'a', emit) == Chord::Result::Handled);
    CHECK(hits == 1);
    CHECK(emitted.empty());

    hits = 0;
    CHECK(chord.feed(INSERT, '\r' | MOD_CTRL, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.feed(INSERT, 'b', emit) == Chord::Result::Handled);
    CHECK(hits == 1);

    // mismatch / timeout replay modified Enter as plain Enter
    hits = 0;
    emitted.clear();
    CHECK(chord.feed(INSERT, '\r' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.feed(INSERT, 'q', emit) == Chord::Result::Pass);
    CHECK(emitted == "\r");
    CHECK(hits == 0);

    emitted.clear();
    CHECK(chord.feed(INSERT, '\r' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    chord.timeout(emit);
    CHECK(emitted == "\r");
    CHECK(!chord.pending());

    // unmapped keys pass straight through
    CHECK(chord.feed(INSERT, 'z', emit) == Chord::Result::Pass);

    // a mode change drops a stale chord
    CHECK(chord.feed(INSERT, ' ' | MOD_SHIFT, emit) ==
          Chord::Result::Buffered);
    CHECK(chord.feed(REPLACE, 'a', emit) == Chord::Result::Pass);
    CHECK(!chord.pending());

    std::printf(failed ? "%d FAILED\n" : "chord: all passed\n", failed);
    return failed != 0;
}
