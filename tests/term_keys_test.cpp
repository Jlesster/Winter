#include "../src/term_keys.hpp"
#include <cstdio>
#include <string>
#include "../src/keys.hpp"

static int g_fail = 0;
static void expect(int key, std::string want, bool app = false) {
    const std::string got = term_encode_key(key, {.app_cursor = app});
    if(got == want) return;
    ++g_fail;
    auto hex = [](const std::string& s) {
        std::string o;
        char b[8];
        for(unsigned char c : s) {
            std::snprintf(b, sizeof b, "%02x ", c);
            o += b;
        }
        return o;
    };
    std::printf("FAIL key=%#x want=[%s] got=[%s]\n",
                key,
                hex(want).c_str(),
                hex(got).c_str());
}

int main() {
    using namespace std::string_literals;
    // plain bytes
    expect('a', "a");
    expect('\r', "\r");
    expect('\t', "\t");
    expect('\x1b', "\x1b");
    expect(BACKSPACE, "\x7f");
    expect(CTRL_BACKSPACE, "\x08");
    expect(0xC3, "\xC3");
    expect(0xA9, "\xA9");
    expect(0, "\0"s);  // Ctrl-Space
    expect(1, "\x01");
    expect(26, "\x1a");  // Ctrl-a, Ctrl-z
    // Alt = ESC prefix
    expect('x' | MOD_ALT, "\x1bx");
    expect(1 | MOD_ALT, "\x1b\x01");
    expect(BACKSPACE | MOD_ALT, "\x1b\x7f");
    expect('\r' | MOD_ALT, "\x1b\r");
    // mods with no legacy encoding are dropped
    expect('\r' | MOD_SHIFT, "\r");
    expect('\r' | MOD_CTRL, "\r");
    expect(' ' | MOD_SHIFT, " ");
    expect('2' | MOD_CTRL, "\0"s);
    expect('8' | MOD_CTRL, "\x7f");
    expect('/' | MOD_CTRL, "\x1f");
    expect('\t' | MOD_SHIFT, "\x1b[Z");
    expect('\t' | MOD_SHIFT | MOD_ALT, "\x1b\x1b[Z");
    // cursor keys
    expect(ARROW_UP, "\x1b[A");
    expect(ARROW_DOWN, "\x1b[B");
    expect(ARROW_RIGHT, "\x1b[C");
    expect(ARROW_LEFT, "\x1b[D");
    expect(HOME, "\x1b[H");
    expect(END, "\x1b[F");
    expect(ARROW_UP, "\x1bOA", true);
    expect(END, "\x1bOF", true);
    expect(ARROW_LEFT | MOD_CTRL, "\x1b[1;5D");
    expect(ARROW_RIGHT | MOD_ALT | MOD_SHIFT, "\x1b[1;4C");
    expect(
        ARROW_UP | MOD_CTRL, "\x1b[1;5A", true);  // modified wins over DECCKM
    expect(END | MOD_CTRL, "\x1b[1;5F");
    // tilde keys
    expect(INS, "\x1b[2~");
    expect(DELETE, "\x1b[3~");
    expect(PAGE_UP, "\x1b[5~");
    expect(PAGE_DOWN, "\x1b[6~");
    expect(PAGE_UP | MOD_SHIFT, "\x1b[5;2~");
    expect(DELETE | MOD_CTRL, "\x1b[3;5~");
    // function keys
    expect(F1, "\x1bOP");
    expect(F4, "\x1bOS");
    expect(F1 | MOD_SHIFT, "\x1b[1;2P");
    expect(F5, "\x1b[15~");
    expect(F6, "\x1b[17~");
    expect(F10, "\x1b[21~");
    expect(F11, "\x1b[23~");
    expect(F12, "\x1b[24~");
    expect(F12 | MOD_CTRL, "\x1b[24;5~");
    // nothing
    expect(PASTE, "");
    expect(NONE, "");
    expect(0x1234, "");
    std::puts(g_fail ? "FAILED" : "all ok");
    return g_fail != 0;
}
