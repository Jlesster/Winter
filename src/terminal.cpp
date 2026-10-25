#include "terminal.hpp"
#include "buflist.hpp"
#include "job.hpp"
#include "loop.hpp"

#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <trux/core.hpp>

#include "keys.hpp"
#include "options.hpp"

namespace {
termios g_orig{};
TermCaps g_caps{};
volatile sig_atomic_t g_resized = 0;

void on_winch(int) {
    g_resized = 1;
    loop_wake();
}  // only touch the flag in here

bool next_byte(char& b, int ms = 50) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    int r;
    while((r = poll(&p, 1, ms)) < 0 && errno == EINTR) {}
    return r > 0 && read(STDIN_FILENO, &b, 1) == 1;
}

int get_cursor_position(int* rows, int* cols);  // below

}  // namespace

const TermCaps& term_caps() { return g_caps; }

// terminal
[[noreturn]] void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void install_winch_handler() {
    struct sigaction sa{};
    sa.sa_handler = on_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // deliberately no SA_RESTART
    if(sigaction(SIGWINCH, &sa, nullptr) == -1) die("sigaction");
}

bool resize_pending() { return g_resized != 0; }
void resize_ack() { g_resized = 0; }

void editor_quit() {
    buffer_remember_cursor();
    job_kill_all();
    trux::ansi::leave_alt_screen();
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
}

void disable_raw_mode() {
    if(g_caps.cursor_shapes &&
       write(STDOUT_FILENO, "\x1b[0 q", 5) != 5) { /* best effort */
    }
    write(STDOUT_FILENO, "\x1b[>4;0m", 7);
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig) == -1) die("tcsetattr");
}

void enable_raw_mode() {
    {
        const char* t = std::getenv("TERM");
        const bool modern = !t || std::string_view(t) != "linux";
        g_caps = {modern, modern};
        if(!modern) opts.diag_undercurl = false;
    }
    if(tcgetattr(STDIN_FILENO, &g_orig) == -1) die("tcgetattr");
    atexit(disable_raw_mode);
    termios raw = g_orig;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    trux::ansi::enter_alt_screen();
    write(STDOUT_FILENO, "\x1b[>4;1m", 7);
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editor_read_key() {
    static int pushed = -1;
    if(pushed != -1) {
        const int k = pushed;
        pushed = -1;
        return k;
    }
    int nread;
    char c;
    for(;;) {
        if(!loop_wait(-1)) return NONE;
        const ssize_t n = read(STDIN_FILENO, &c, 1);
        if(n == 1) break;
        if(n == -1 && errno != EAGAIN && errno != EINTR) die("read");
    }
    if(c != '\x1b') return static_cast<unsigned char>(c);
    char b;
    if(!next_byte(b)) return '\x1b';
    if(b == 'O') {
        if(!next_byte(b)) return '\x1b';
        switch(b) {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;
            case 'H': return HOME;
            case 'F': return END;
        }
        return NONE;
    }
    if(b != '[') {
        pushed = static_cast<unsigned char>(b);
        return '\x1b';
    }
    int p[3] = {0, 0, 0};
    int np = 0;
    bool sub = false;
    const auto ctrl = [&](int mod) { return mod > 1 && ((mod - 1) & 4); };
    for(int i = 0; i < 16; ++i) {
        if(!next_byte(b)) return '\x1b';
        if(b >= '0' && b <= '9') {
            if(!sub && np < 3) p[np] = p[np] * 10 + (b - '0');
        } else if(b == ':') {
            sub = true;
        } else if(b == ';') {
            sub = false;
            ++np;
        } else if(b >= 0x40 && b <= 0x7E) {
            switch(b) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME;
                case 'F': return END;
                case 'u':  // kitty / CSI-u: 127;5u
                    if(ctrl(p[1])) {
                        if(p[0] == 127) return CTRL_BACKSPACE;
                        if(p[0] >= 'a' && p[0] <= 'z') return p[0] & 0x1f;
                    }
                    break;
                case '~':
                    switch(p[0]) {
                        case 1:
                        case 7: return HOME;
                        case 3: return DELETE;
                        case 4:
                        case 8: return END;
                        case 5: return PAGE_UP;
                        case 6: return PAGE_DOWN;
                        case 27:
                            if(p[2] == 127 && ctrl(p[1])) return CTRL_BACKSPACE;
                            break;
                    }
                    break;
            }
            return NONE;
        }
    }
    return NONE;
}

namespace {
int get_cursor_position(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while(i < sizeof(buf) - 1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}
}  // namespace

int query_window_size(int* rows, int* cols) {
    winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) return -1;
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}

int get_window_size(int* rows, int* cols) {
    winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

namespace {

static int decscursr(const CursorStyle& c) {
    if(c.shape == CursorShape::Default) return 0;
    const int steady = 2 * static_cast<int>(c.shape);
    return c.blink ? steady - 1 : steady;
}

static int cursor_style_for(EditorMode m) {
    switch(m) {
        case EditorMode::INSERT: return decscursr(opts.cursor_insert);
        case EditorMode::VISUAL: return decscursr(opts.cursor_visual);
        default: return decscursr(opts.cursor_normal);
    }
}

static void set_cursor_style(int style) {
    static int current = -1;  // what the terminal has right now
    if(style == current) return;
    current = style;
    char buf[16];
    const int n = std::snprintf(buf, sizeof buf, "\x1b[%d q", style);
    if(n > 0 && write(STDOUT_FILENO, buf, static_cast<size_t>(n)) != n) return;
}

}  // namespace

void set_cursor_color(uint32_t rgb) {
    if(!g_caps.cursor_shapes) return;
    static uint32_t current = 0xFFFFFFFF;
    if(rgb == current) return;
    current = rgb;
    char buf[32];
    const int n =
        rgb == 0xFFFFFFFF
            ? std::snprintf(buf, sizeof buf, "\x1b]112\x1b\\")
            : std::snprintf(
                  buf, sizeof buf, "\x1b]12;#%06x\x1b\\", rgb & 0xFFFFFFFF);
    if(n > 0 && write(STDOUT_FILENO, buf, static_cast<size_t>(n)) != n) return;
}

void set_cursor_for_mode(EditorMode m) {
    if(g_caps.cursor_shapes) set_cursor_style(cursor_style_for(m));
}
