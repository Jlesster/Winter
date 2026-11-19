#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <random>
#include <string>

#include "../src/vt.hpp"

static int g_fail = 0, g_run = 0;

static void put8(std::string& o, char32_t c) {
    if(c < 0x80) o += static_cast<char>(c);
    else if(c < 0x800) {
        o += static_cast<char>(0xC0 | c >> 6);
        o += static_cast<char>(0x80 | (c & 0x3F));
    } else if(c < 0x10000) {
        o += static_cast<char>(0xE0 | c >> 12);
        o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | c >> 18);
        o += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (c & 0x3F));
    }
}

static std::string row_text(const Vt& v, int r) {
    std::string s;
    for(int c = 0; c < v.cols(); ++c) {
        const VtCell& k = v.cell(r, c);
        if(k.w == 0) continue;
        put8(s, k.cp);
    }
    while(!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static std::string dump(const Vt& v) {
    std::string s;
    int last = v.rows() - 1;
    while(last > 0 && row_text(v, last).empty()) --last;
    for(int r = 0; r <= last; ++r) s += row_text(v, r) + "\n";
    return s;
}

#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_run;                                                        \
        if(!(cond)) {                                                   \
            ++g_fail;                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while(0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        ++g_run;                                                        \
        const auto _a = (a);                                            \
        const auto _b = (b);                                            \
        if(!(_a == _b)) {                                               \
            ++g_fail;                                                   \
            std::printf(                                                \
                "FAIL %s:%d  %s\n  got:      [%s]\n  expected: [%s]\n", \
                __FILE__,                                               \
                __LINE__,                                               \
                #a,                                                     \
                std::string(_a).c_str(),                                \
                std::string(_b).c_str());                               \
        }                                                               \
    } while(0)

static void test_basic() {
    Vt v(5, 10);
    v.feed("hello\r\nworld");
    CHECK_EQ(dump(v), "hello\nworld\n");
    CHECK(v.cursor().x == 5 && v.cursor().y == 1);

    // deferred wrap: the 10th char leaves the cursor pending, the 11th wraps
    Vt w(3, 10);
    w.feed("0123456789");
    CHECK(w.cursor().x == 9 && w.cursor().y == 0);
    w.feed("A");
    CHECK_EQ(dump(w), "0123456789\nA\n");
    CHECK(w.cursor().x == 1 && w.cursor().y == 1);

    // CR after a full line must not wrap
    Vt x(3, 5);
    x.feed("abcde\rX");
    CHECK_EQ(dump(x), "Xbcde\n");
}

static void test_cursor_erase() {
    Vt v(5, 10);
    v.feed("\x1b[2;3Hxy\x1b[1;1Hab");
    CHECK_EQ(dump(v), "ab\n  xy\n");
    v.feed("\x1b[2J");
    CHECK_EQ(dump(v), "\n");
    v.feed("\x1b[1;1Habcdefghij\x1b[1;5H\x1b[K");
    CHECK_EQ(dump(v), "abcd\n");
    v.feed("\x1b[1;1Habcdefghij\x1b[1;5H\x1b[1K");
    CHECK_EQ(dump(v), "     fghij\n");
    // CUU/CUD/CUF/CUB clamp
    Vt c(4, 6);
    c.feed("\x1b[99B\x1b[99C");
    CHECK(c.cursor().x == 5 && c.cursor().y == 3);
    c.feed("\x1b[99A\x1b[99D");
    CHECK(c.cursor().x == 0 && c.cursor().y == 0);
}

static void test_sgr() {
    Vt v(2, 20);
    v.feed(
        "\x1b[1;31mA\x1b[0mB\x1b[38;5;196mC\x1b[38;2;1;2;3mD\x1b[48:2::9:8:"
        "7mE\x1b[4:3mF\x1b[mG");
    CHECK(v.cell(0, 0).st.attrs & ATTR_BOLD);
    CHECK(v.cell(0, 0).st.fg == 0xf38ba8);
    CHECK(v.cell(0, 1).st == Style{});
    CHECK(v.cell(0, 2).st.fg == 0xff0000);
    CHECK(v.cell(0, 3).st.fg == 0x010203);
    CHECK(v.cell(0, 4).st.bg == 0x090807);
    CHECK(v.cell(0, 5).st.attrs & ATTR_UNDERCURL);
    CHECK(v.cell(0, 6).st == Style{});
    // erased cells take the background colour (BCE)
    Vt b(2, 4);
    b.feed("\x1b[44m\x1b[2J");
    CHECK(b.cell(1, 3).st.bg == 0x89b4fa);
}

static void test_scroll() {
    Vt v(4, 5, 100);
    v.feed("1\r\n2\r\n3\r\n4\r\n5\r\n6");
    CHECK_EQ(dump(v), "3\n4\n5\n6\n");
    CHECK(v.scrollback_lines() == 2);
    // scroll region
    Vt r(5, 5, 100);
    r.feed("a\r\nb\r\nc\r\nd\r\ne\x1b[2;4r\x1b[4;1H\n");
    CHECK_EQ(dump(r), "a\nc\nd\n\ne\n");
    CHECK(r.scrollback_lines() == 0);  // a partial region never feeds history
    r.feed("\x1b[2;1H\x1bM");
    CHECK_EQ(dump(r), "a\n\nc\nd\ne\n");
    // IL / DL
    Vt i(5, 5);
    i.feed("a\r\nb\r\nc\r\nd\r\ne\x1b[2;1H\x1b[2L");
    CHECK_EQ(dump(i), "a\n\n\nb\nc\n");
    i.feed("\x1b[2;1H\x1b[2M");
    CHECK_EQ(dump(i), "a\nb\nc\n");
}

static void test_chars() {
    Vt v(2, 10);
    v.feed("abcdef\x1b[1;3H\x1b[2@");
    CHECK_EQ(dump(v), "ab  cdef\n");
    v.feed("\x1b[1;3H\x1b[2P");
    CHECK_EQ(dump(v), "abcdef\n");
    v.feed("\x1b[1;3H\x1b[2X");
    CHECK_EQ(dump(v), "ab  ef\n");
    Vt ins(1, 8);
    ins.feed("abcd\x1b[1;2H\x1b[4hZZ\x1b[4l");
    CHECK_EQ(dump(ins), "aZZbcd\n");
    Vt rep(1, 8);
    rep.feed("x\x1b[3b");
    CHECK_EQ(dump(rep), "xxxx\n");
}

static void test_utf8_wide() {
    Vt v(2, 6);
    v.feed(
        "a\xE4\xB8\xAD"
        "b");  // a 中 b
    CHECK(v.cell(0, 1).w == 2 && v.cell(0, 2).w == 0);
    CHECK(v.cursor().x == 4);
    CHECK_EQ(dump(v),
             "a\xE4\xB8\xAD"
             "b\n");
    // a wide char that does not fit wraps whole
    Vt w(2, 4);
    w.feed("abc\xE4\xB8\xAD");
    CHECK_EQ(dump(w), "abc\n\xE4\xB8\xAD\n");
    // overwriting half of a wide char clears the other half
    Vt o(1, 6);
    o.feed("\xE4\xB8\xAD\x1b[1;2HX");
    CHECK(o.cell(0, 0).cp == U' ' && o.cell(0, 1).cp == U'X');
    // invalid bytes become U+FFFD, split sequences still decode
    Vt s(1, 6);
    s.feed("\xE4");
    s.feed("\xB8");
    s.feed("\xAD");
    CHECK(s.cell(0, 0).cp == 0x4E2D);
    s.feed("\xFF");
    CHECK(s.cell(0, 2).cp == 0xFFFD);
}

static void test_alt_screen() {
    Vt v(3, 6, 100);
    v.feed("main\r\n");
    v.feed("\x1b[?1049h");
    CHECK(v.alt_screen());
    CHECK_EQ(dump(v), "\n");
    v.feed("ALT\r\n\r\n\r\n\r\n");
    CHECK(v.scrollback_lines() == 0);  // the alt screen never feeds history
    v.feed("\x1b[?1049l");
    CHECK(!v.alt_screen());
    CHECK_EQ(dump(v), "main\n");
    CHECK(v.cursor().x == 0 && v.cursor().y == 1);
}

static void test_charset() {
    Vt v(1, 8);
    v.feed("\x1b(0lqk\x1b(Bq");
    CHECK_EQ(dump(v), "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90q\n");
    Vt s(1, 8);
    s.feed(
        "\x1b)0\x0e"
        "q\x0f"
        "q");  // G1 = graphics, SO, q, SI, q
    CHECK_EQ(dump(s), "\xE2\x94\x80q\n");
}

static void test_osc_replies_modes() {
    Vt v(3, 10);
    v.feed("\x1b]0;my title\x07");
    CHECK_EQ(v.title(), "my title");
    v.feed("\x1b]2;second\x1b\\");
    CHECK_EQ(v.title(), "second");
    v.feed("\x1b]7;file://h/tmp\x07");
    CHECK_EQ(v.cwd(), "file://h/tmp");
    v.feed("\x1b[c");
    CHECK_EQ(v.take_reply(), "\x1b[?62;22c");
    v.feed("\x1b[2;4H\x1b[6n");
    CHECK_EQ(v.take_reply(), "\x1b[2;4R");
    v.feed("\x1b[5n");
    CHECK_EQ(v.take_reply(), "\x1b[0n");
    v.feed("\x1b[?2004h\x1b[?1h\x1b[?25l");
    CHECK(v.bracketed_paste() && v.app_cursor() && !v.cursor().visible);
    v.feed("\x1b[?2004$p");
    CHECK_EQ(v.take_reply(), "\x1b[?2004;1$y");
    v.feed("\x1b[?9999$p");
    CHECK_EQ(v.take_reply(), "\x1b[?9999;0$y");
    v.feed("\x1b[?1000h\x1b[?1006h");
    CHECK(v.mouse_mode() == Vt::Mouse::Press && v.mouse_sgr());
    v.feed("\a");
    CHECK(v.take_bell());
    // unknown sequences are swallowed, not printed
    Vt u(1, 20);
    u.feed(
        "a\x1bP1$rq\x1b\\"
        "b\x1b_apc\x1b\\"
        "c\x1b[?99;99;y"
        "d");
    CHECK_EQ(dump(u), "abcd\n");
}

static void test_tabs_bs() {
    Vt v(1, 20);
    v.feed("a\tb\tc");
    CHECK(v.cell(0, 8).cp == U'b' && v.cell(0, 16).cp == U'c');
    v.feed("\x1b[1;1H\x1b[3g");  // clear all stops
    v.feed("\x1b[1;5H\x1bH\x1b[1;1H\tX");
    CHECK(v.cell(0, 4).cp == U'X');
    Vt b(1, 5);
    b.feed("ab\bX");
    CHECK_EQ(dump(b), "aX\n");
}

static void test_origin() {
    Vt v(6, 6);
    v.feed("\x1b[3;5r\x1b[?6h\x1b[1;1HX");
    CHECK(v.cell(2, 0).cp == U'X');
    v.feed("\x1b[99;1HY");
    CHECK(v.cell(4, 0).cp == U'Y');
}

// Feeding in one go and byte by byte must give the same screen.
static void test_chunking(unsigned seed) {
    std::mt19937 rng(seed);
    const char* frags[] = {"abc",
                           "\x1b[1;31m",
                           "\x1b[0m",
                           "\r\n",
                           "\x1b[2;3H",
                           "\xE4\xB8\xAD",
                           "\x1b[K",
                           "\x1b]0;t\x07",
                           "\x1b[?1049h",
                           "\x1b[?1049l",
                           "\t",
                           "\x1b[3L",
                           "\x1b[2M",
                           "\x1b[38:2::1:2:3m",
                           "\x1b[2J",
                           "\x1b(0qx\x1b(B",
                           "\x1bM",
                           "\x1b[5S",
                           "xyz123"};
    std::string data;
    for(int i = 0; i < 400; ++i)
        data += frags[rng() % (sizeof frags / sizeof *frags)];
    Vt a(8, 20), b(8, 20);
    a.feed(data);
    for(const char ch : data) b.feed(std::string_view(&ch, 1));
    CHECK_EQ(dump(a), dump(b));
    CHECK(a.cursor().x == b.cursor().x && a.cursor().y == b.cursor().y);
    bool same = true;
    for(int r = 0; r < 8; ++r)
        for(int c = 0; c < 20; ++c)
            if(!(a.cell(r, c).st == b.cell(r, c).st)) same = false;
    CHECK(same);
}

// Random bytes must never crash or leave the cursor out of range.
static void test_fuzz(unsigned seed) {
    std::mt19937 rng(seed);
    Vt v(
        1 + static_cast<int>(rng() % 12), 1 + static_cast<int>(rng() % 30), 50);
    for(int round = 0; round < 300; ++round) {
        std::string d;
        const int n = static_cast<int>(rng() % 80);
        for(int i = 0; i < n; ++i) {
            switch(rng() % 4) {
                case 0: d += static_cast<char>(rng() & 0xFF); break;
                case 1:
                    d += "\x1b[";
                    d += std::to_string(rng() % 40);
                    d += ';';
                    d += std::to_string(rng() % 40);
                    d += "HJKLMPX@ABCDGdmrSTlhb"[rng() % 21];
                    break;
                case 2:
                    d += "\x1b[?";
                    d += std::to_string(rng() % 3000);
                    d += "hl"[rng() % 2];
                    break;
                default: d += static_cast<char>(0x20 + rng() % 0x5f); break;
            }
        }
        v.feed(d);
        if(rng() % 20 == 0)
            v.resize(1 + static_cast<int>(rng() % 12),
                     1 + static_cast<int>(rng() % 30));
        const auto c = v.cursor();
        if(c.x < 0 || c.x >= v.cols() || c.y < 0 || c.y >= v.rows()) {
            CHECK(false);
            return;
        }
        (void)v.take_reply();
    }
    CHECK(true);
}

static void test_resize() {
    Vt v(4, 8, 100);
    v.feed("1\r\n2\r\n3\r\n4");
    v.resize(2, 8);  // cursor on row 3: two rows go to history
    CHECK_EQ(dump(v), "3\n4\n");
    CHECK(v.scrollback_lines() == 2);
    v.resize(5, 4);
    CHECK(v.rows() == 5 && v.cols() == 4);
    CHECK(v.cursor().y < 5);
}

int main(int argc, char** argv) {
    if(argc > 1 &&
       std::string(argv[1]) == "dump") {  // test.cpp doubles as the dump tool
        // usage: test dump ROWS COLS < payload ; prints grid then cursor
        const int rows = std::atoi(argv[2]), cols = std::atoi(argv[3]);
        Vt v(rows, cols, 0);
        std::string in((std::istreambuf_iterator<char>(std::cin)), {});
        v.feed(in);
        for(int r = 0; r < rows; ++r)
            std::printf("%s\n", row_text(v, r).c_str());
        std::printf("CURSOR %d %d\n",
                    v.cursor().x + (v.cursor().pending ? 1 : 0),
                    v.cursor().y);
        return 0;
    }
    test_basic();
    test_cursor_erase();
    test_sgr();
    test_scroll();
    test_chars();
    test_utf8_wide();
    test_alt_screen();
    test_charset();
    test_osc_replies_modes();
    test_tabs_bs();
    test_origin();
    test_resize();
    for(unsigned s = 1; s <= 200; ++s) test_chunking(s);
    for(unsigned s = 1; s <= 300; ++s) test_fuzz(s);
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
