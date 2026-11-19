// Unit tests for num.cpp (no editor state needed).
#include "../src/num.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

static int g_fail = 0, g_run = 0;

// Apply bump() to `line` with the cursor at the '|' marker (removed first).
static std::string run(std::string line, int64_t delta, num::Opts o = {}) {
    const size_t col = line.find('|');
    if(col != std::string::npos) line.erase(col, 1);
    const auto e =
        num::bump(line, col == std::string::npos ? 0 : col, delta, o);
    if(!e) return "<none>";
    line.replace(e->start, e->len, e->text);
    return line;
}

static void eq(const std::string& got, const std::string& want,
               const char* what, int ln) {
    ++g_run;
    if(got == want) return;
    ++g_fail;
    std::printf("FAIL line %d: %s\n  got:  %s\n  want: %s\n",
                ln,
                what,
                got.c_str(),
                want.c_str());
}
#define T(in, d, want) eq(run(in, d), want, in " " #d, __LINE__)
#define TI(in, d, want)                                            \
    do {                                                           \
        num::Opts o_;                                              \
        o_.int_only = true;                                        \
        eq(run(in, d, o_), want, "int_only " in " " #d, __LINE__); \
    } while(0)
#define TF(in, d, fmts, want)                                           \
    do {                                                                \
        num::Opts o_;                                                   \
        o_.formats = *num::parse_formats(fmts);                         \
        eq(run(in, d, o_), want, "fmts " fmts " " in " " #d, __LINE__); \
    } while(0)

int main() {
    // decimal
    T("|5", 1, "6");
    T("x = |-5", 1, "x = -4");
    T("-|1", 1, "0");
    T("|0", -1, "-1");
    T("a-|5", 1, "a-6");  // minus is an operator after a word char
    T("(a)-|5", 1, "(a)-6");
    T("007", 1, "010");  // octal is on by default
    TF("007", 1, "-octal", "008");
    T("0|09", 1, "010");
    TF("|-007", 10, "-octal", "003");
    T("|-007", 10, "-021");  // octal is unsigned: the - stays
    T("foo |12 bar 7", 3, "foo 15 bar 7");
    T("foo 12 |bar 7", 3, "foo 12 bar 10");  // next number after the cursor
    T("no digits", 1, "<none>");
    T("9223372036854775807", 5, "9223372036854775807");  // saturates
    T("vec|3", 1, "vec4");
    T("1|0u", 1, "11u");
    T("10|u", 1, "<none>");  // cursor on the suffix: nothing after it
    T("+|5", 1, "+6");

    // hex / bin
    T("0x|ff", 1, "0x100");
    T("0xFF", 1, "0x100");
    T("0xfe", 1, "0xff");
    T("0xFE", 1, "0xFF");
    T("0xaF", 1, "0xB0");  // case follows the last letter
    T("0x00ff", 1, "0x0100");
    T("0x0", -1, "0xffffffffffffffff");  // wraps at 64 bits
    T("0xffffffffffffffff", 1, "0x0");
    T("0XfF", 1, "0X100");
    T("0b101", 1, "0b110");
    T("0b0011", 1, "0b0100");
    T("0b1", -1, "0b0");
    T("0xFFull", 1, "0x100ull");

    // octal
    T("0755", 1, "0756");
    T("0777", 1, "01000");
    T("0100", -1, "0077");  // width kept
    T("010", -1, "007");
    T("00755", 1, "00756");
    T("0o17", 1, "0o20");
    T("08", 1, "09");  // not octal -> decimal
    T("0", 1, "1");

    // reals
    T("3.14", 1, "4.14");
    T("-0.5", 1, "0.5");
    T("0.5", -1, "-0.5");
    T("1.5e3", 1, "2.5e3");
    T("-1.0", 1, "0.0");
    T("1.0f", 1, "2.0f");
    T("9.99", 1, "10.99");
    T("0.05", 1, "1.05");

    // versions
    T("1.2.3", 1, "1.2.4");
    T("v1.2.3", 1, "v1.2.4");
    T("|1.2.3", 1, "1.2.4");
    T("1.2.3.9", 1, "1.2.3.10");
    T("1.2.09", 1, "1.2.10");
    T("1.2.0", -1, "1.2.0");  // floors at 0

    // int_only
    TI("1.|2.3", 1, "1.3.3");
    TI("|1.2.3", 1, "2.2.3");
    TI("1.2.|3", 1, "1.2.4");
    TI("3.1|4", 1, "3.15");
    TI("3.9|9", 1, "4.00");
    TI("3.0|0", -1, "2.99");
    TI("|3.14",
       1,
       "4.14");  // integer-part column: whole mode semantics? no: int run
    TI("3.14e|5", 1, "3.14e6");
    TI("0x|ff", 1, "0x100");
    TI("-|5", 1, "-4");

    // numformats
    TF("0xff", 1, "none", "1xff");  // hex off: just the decimal 0
    TF("0755", 1, "-octal", "0756");
    TF("3.14", 1, "-real", "4.14");
    TF("vec3", 1, "-ident", "<none>");
    TF("1.2.3", 1, "-version", "2.2.3");

    // parse/format
    {
        const auto f = num::parse_formats("bin,hex");
        eq(f ? num::format_formats(*f) : "<err>",
           "bin,hex",
           "parse bin,hex",
           __LINE__);
        std::string err;
        const auto bad = num::parse_formats("bin,-hex", &err);
        eq(bad ? "ok" : "err", "err", "mixed rel/abs rejected", __LINE__);
        const auto bad2 = num::parse_formats("wat", &err);
        eq(bad2 ? "ok" : "err", "err", "unknown rejected", __LINE__);
        const auto all = num::parse_formats("all");
        eq(all && *all == num::kAll ? "ok" : "bad", "ok", "all", __LINE__);
    }

    std::printf("%d/%d passed\n", g_run - g_fail, g_run);
    return g_fail ? 1 : 0;
}
