#include <asm-generic/ioctls.h>
#include <sys/stat.h>
#include <algorithm>
#include <csignal>
#include <format>
#include <ios>
#include <iterator>
#include <trux/core.hpp>
#include "buffer.hpp"
#include "core.hpp"
#include "motion.hpp"
#include "piece.hpp"
#include "syntax.hpp"
#include "utf8.hpp"

#include <cctype>
#include <climits>
#include <ctime>
#include <fstream>
#include <map>

#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>

enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    DELETE,
    HOME,
    END,
    NONE,
};

// data
struct EditorConfig {
    bool eol = true;
    int cx, cy = 0;
    int rx = 0;
    int want_rx = 0;
    int row_offset = 0;
    int col_offset = 0;
    int term_rows = 0;
    int term_cols = 0;
    std::string orig{};
    PieceTable pt{};
    Syntax syntax{};
    int num_rows() const { return static_cast<int>(pt.line_count()); }
    std::string filename = "";
    std::string status_message = "";
    time_t status_message_time = 0;
    termios orig_termios{};
    EditorMode mode = EditorMode::NORMAL;
};

EditorConfig E;

// filetypes
void editor_refresh_screen();
std::string editor_prompt(const std::string_view prompt,
                          void (*callback)(std::string, int));
template <typename... Args>
void editor_set_status_message(std::format_string<Args...> fmt, Args&&... args);

// terminal
void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

static volatile sig_atomic_t g_resized = 0;
static void on_winch(int) { g_resized = 1; }

void install_winch_handler() {
    struct sigaction sa{};
    sa.sa_handler = on_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGWINCH, &sa, nullptr) == -1) die("sigaction");
}

void editor_quit() {
    trux::ansi::leave_alt_screen();
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
}

void disable_raw_mode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enable_raw_mode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw_mode);
    termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    trux::ansi::enter_alt_screen();
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
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(g_resized) return NONE;
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    if(c != '\x1b') return static_cast<unsigned char>(c);
    char b;
    if(read(STDIN_FILENO, &b, 1) != 1) return '\x1b';
    if(b == 'O') {
        if(read(STDIN_FILENO, &b, 1) != 1) return '\x1b';
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
    int param = 0;
    bool first = true;
    for(int i = 0; i < 16; ++i) {
        if(read(STDIN_FILENO, &b, 1) != 1) return '\x1b';
        if(b >= '0' && b <= '9') {
            if(first) param = param * 10 + (b - '0');
        } else if(b == ';') {
            first = false;
        } else if(b >= 0x40 && b <= 0x7E) {
            switch(b) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME;
                case 'F': return END;
                case '~':
                    switch(param) {
                        case 1:
                        case 7: return HOME;
                        case 3: return DELETE;
                        case 4:
                        case 8: return END;
                        case 5: return PAGE_UP;
                        case 6: return PAGE_DOWN;
                    }
            }
            return NONE;
        }
    }
    return NONE;
}

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

// row helpers

size_t line_len(size_t y) { return E.pt.line_end(y) - E.pt.line_start(y); }
bool is_ctl(char32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp < 0xA0); }

int cell_width(char32_t cp, int rx) {
    if(cp == '\t') return WINTER_TAB_STOP - (rx % WINTER_TAB_STOP);
    if(is_ctl(cp)) return 1;
    return utf8_width(cp);
}

int snap_cx(std::string_view line, int cx) {
    if(cx <= 0) return 0;
    if(cx >= static_cast<int>(line.size()))
        return static_cast<int>(line.size());
    return static_cast<int>(utf8_prev(line, utf8_next(line, cx)));
}

int cx_to_rx(std::string_view line, int cx) {
    int rx = 0;
    for(int j = 0;
        j < static_cast<int>(line.size()) && static_cast<int>(j) < cx;) {
        const Decoded d = utf8_decode(line, j);
        rx += cell_width(d.cp, rx);
        j += d.len;
    }
    return rx;
}

int rx_to_cx(std::string_view line, int rx) {
    int cur = 0;
    size_t i = 0;
    while(i < line.size()) {
        const Decoded d = utf8_decode(line, i);
        cur += cell_width(d.cp, cur);
        if(cur > rx) return static_cast<int>(i);
        i += d.len;
    }
    return static_cast<int>(i);
}

Screen scr;
bool g_scroll = false;

int text_width(std::string_view s) {
    int w = 0;
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        w += cell_width(d.cp, w);
        i += d.len;
    }
    return w;
}

int put_text(int x, int y, std::string_view s, int limit, Style st) {
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        i += d.len;
        const int w = cell_width(d.cp, 0);
        if(w == 0) {
            if(x > 0) scr.at(x - 1, y).mark = d.cp;
            continue;
        }
        if(x + w > limit) break;
        scr.at(x, y) = Cell{d.cp, static_cast<uint8_t>(w), st};
        if(w == 2) scr.at(x + 1, y) = Cell{U'\0', 0, st};
        x += w;
    }
    return x;
}

void put_line(int y, std::string_view line, int col_off, int cols,
              const std::vector<Span>& spans = {}) {
    const int end = col_off + cols;
    int rx = 0;
    int last = -1;
    size_t si = 0;
    for(size_t i = 0; i < line.size() && rx < end;) {
        const size_t at = i;
        const Decoded d = utf8_decode(line, i);
        i += d.len;
        while(si < spans.size() && spans[si].end <= at) ++si;
        const Style st = (si < spans.size() && spans[si].start <= at)
                             ? spans[si].st
                             : Style{};
        const char32_t cp = d.cp;
        const int w = cell_width(cp, rx);
        if(w == 0) {
            if(last >= 0) scr.at(last, y).mark = cp;
            continue;
        }
        const int next = rx + w;
        if(next > col_off) {
            const int x = rx - col_off;
            if(cp == '\t' || rx < col_off || next > end) {
                for(int k = std::max(rx, col_off); k < std::min(next, end); k++)
                    scr.at(k - col_off, y) = Cell{};
                last = -1;
            } else if(is_ctl(cp)) {
                const char shown =
                    (cp <= 26) ? static_cast<char>('@' + cp) : '?';
                scr.at(x, y) = Cell{static_cast<char32_t>(shown),
                                    1,
                                    Style{.attrs = ATTR_REVERSE}};
                last = -1;
            } else {
                scr.at(x, y) = Cell{cp, static_cast<uint8_t>(w), st};
                if(w == 2) scr.at(x + 1, y) = Cell{u'\0', 0, st};
                last = x;
            }
        }
        rx = next;
    }
}

// cursor offset modes

size_t cursor_offset() { return E.pt.from_linecol(E.cy, E.cx); }

void set_cursor_offset(size_t pos) {
    const LineCol lc = E.pt.to_linecol(pos);
    E.cy = static_cast<int>(lc.line);
    E.cx = static_cast<int>(lc.col);
}

void set_mode(EditorMode m) {
    if(E.mode == m) return;
    if(E.mode == INSERT) E.pt.end_group();
    if(m == INSERT) E.pt.begin_group();
    E.mode = m;
}

// edits

void editor_insert_char(int c) {
    const char ch = static_cast<char>(c);
    E.pt.insert(cursor_offset(), std::string_view(&ch, 1));
    E.cx++;
}

void editor_insert_tab() {
#if WINTER_EXPAND_TAB
    const int rx = cx_to_rx(E.pt.line_text(E.cy), E.cx);
    const int n = WINTER_SHIFT_WIDTH - (rx % WINTER_SHIFT_WIDTH);
    E.pt.insert(cursor_offset(), std::string(static_cast<size_t>(n), ' '));
    E.cx += n;
#else
    editor_insert_char('\t');
#endif
}

static const std::string kIndentUnit =
    WINTER_EXPAND_TAB ? std::string(WINTER_SHIFT_WIDTH, ' ')
                      : std::string("\t");

static char closer_for(char c) {
    switch(c) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '<': return '>';
        default: return 0;
    }
}

static bool is_closer(char c) {
    return c == ')' || c == ']' || c == '}' || c == '>';
}
static bool is_quote(char c) { return c == '"' || c == '\'' || c == '`'; }
static bool is_word(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static std::string_view leading_ws(std::string_view line) {
    size_t i = 0;
    while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.substr(0, i);
}

static bool is_empty_pair(char prev, char next) {
    if(!next) return false;
    return closer_for(prev) == next || (is_quote(prev) && prev == next);
}

void editor_type_char(int c) {
    const char ch = static_cast<char>(c);
    const std::string line = E.pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const char prev = cx > 0 ? line[cx - 1] : 0;
    const char next = cx < line.size() ? line[cx] : 0;
    if((is_closer(ch) || is_quote(ch)) && next == ch) {
        E.cx++;
        return;
    }
    if(is_closer(ch)) {
        if(line.find_first_not_of(" \t") >= cx) {
            size_t drop = 0;
            if(prev == '\t') drop = 1;
            else
                while(drop < kIndentUnit.size() && drop < cx &&
                      line[cx - 1 - drop] == ' ')
                    ++drop;
            if(drop) {
                E.pt.remove(cursor_offset() - drop, drop);
                E.cx -= static_cast<int>(drop);
            }
        }
        editor_insert_char(c);
        return;
    }
    const bool room = next == 0 || next == ' ' || next == '\t' ||
                      is_closer(next) || next == ',' || next == ';';
    if(const char cl = closer_for(ch)) {
        E.pt.insert(cursor_offset(),
                    room ? std::string{ch, cl} : std::string{ch});
        E.cx++;
        return;
    }
    if(is_quote(ch)) {
        const bool apostrophe = ch == '\'' && is_word(prev);
        const bool pair = room && !apostrophe && prev != ch;
        if(pair) {
            E.pt.insert(cursor_offset(), std::string{ch, ch});
            E.cx++;
        } else editor_insert_char(c);
        return;
    }
    editor_insert_char(c);
}

void editor_insert_newline() {
    const std::string line = E.pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const std::string_view before = std::string_view(line).substr(0, cx);
    const std::string_view after = std::string_view(line).substr(cx);
    const std::string indent(leading_ws(before));
    const size_t last = before.find_last_not_of(" \t");
    const char prev = last == std::string_view::npos ? 0 : before[last];
    if(last == std::string_view::npos && cx > 0) {
        E.pt.remove(cursor_offset() - cx, cx);
        E.cx = 0;
    }
    std::string ins = "\n" + indent;
    size_t new_cx = indent.size();
    if(const char cl = closer_for(prev)) {
        ins += kIndentUnit;
        new_cx += kIndentUnit.size();
        if(after.starts_with(cl)) ins += "\n" + indent;
    }
    E.pt.insert(cursor_offset(), ins);
    E.cy++;
    E.cx = static_cast<int>(new_cx);
}

static std::string indent_below(std::string_view line) {
    std::string indent(leading_ws(line));
    const size_t last = line.find_last_not_of(" \t");
    if(last != std::string_view::npos && closer_for(line[last]))
        indent += kIndentUnit;
    return indent;
}

void editor_open_below() {
    const std::string indent = indent_below(E.pt.line_text(E.cy));
    E.pt.insert(E.pt.line_end(E.cy), "\n" + indent);
    E.cy++;
    E.cx = static_cast<int>(indent.size());
}

void editor_open_above() {
    const std::string indent(leading_ws(E.pt.line_text(E.cy)));
    E.pt.insert(E.pt.line_start(E.cy), indent + "\n");
    E.cx = static_cast<int>(indent.size());
}

void editor_delete_char() {
    const size_t pos = cursor_offset();
    if(pos == 0) return;
    if(E.cx > 0) {
        const std::string line = E.pt.line_text(E.cy);
        const char next = E.cx < static_cast<int>(line.size()) ? line[E.cx] : 0;
        if(is_empty_pair(line[E.cx - 1], next)) {
            E.pt.remove(pos - 1, 2);
            E.cx--;
            return;
        }
        const int from = static_cast<int>(utf8_prev(line, E.cx));
        E.pt.remove(pos - (E.cx - from), E.cx - from);
        E.cx = from;
    } else {
        const int prev_len = static_cast<int>(line_len(E.cy - 1));
        E.pt.remove(pos - 1, 1);
        E.cy--;
        E.cx = prev_len;
    }
}

void editor_delete_forward() {
    const std::string line = E.pt.line_text(E.cy);
    const size_t pos = cursor_offset();
    if(E.cx < static_cast<int>(line.size())) {
        const int to = static_cast<int>(utf8_next(line, E.cx));
        E.pt.remove(pos, static_cast<size_t>(to - E.cx));
    } else if(E.cy < E.num_rows() - 1) {
        E.pt.remove(pos, 1);
    }
}

// file io

void editor_open(const std::string& filename) {
    E.filename = filename;
    std::ifstream in(filename, std::ios::binary);
    if(!in) die("fopen");
    E.orig.assign(std::istreambuf_iterator<char>(in),
                  std::istreambuf_iterator<char>());
    E.eol = E.orig.empty() || E.orig.back() == '\n';
    std::string_view view = E.orig;
    if(!E.orig.empty() && E.orig.back() == '\n') view.remove_suffix(1);
    E.pt = PieceTable(view);
    if(E.syntax.attach(filename)) E.syntax.parse(E.pt, E.eol);
}

// find

void editor_find_callback(std::string query, int key) {
    static int last_match = -1;
    static std::string saved_query = "";
    int direction = 1;
    if(E.mode == EditorMode::NORMAL && (key == 'n' || key == 'N')) {
        if(saved_query == "") {
            editor_set_status_message("No previous search");
            return;
        }
        query = saved_query;
        direction = (key == 'N') ? -1 : 1;
    } else if(key == '\r') {
        saved_query = query;
        return;
    } else {
        last_match = -1;
        if(key == '\x1b' || query[0] == '\0') return;
    }
    if(last_match == -1) direction = 1;
    if(last_match >= E.num_rows()) last_match = -1;
    int current = last_match;
    int i;
    for(i = 0; i < E.num_rows(); i++) {
        current += direction;
        if(current == -1) current = E.num_rows() - 1;
        else if(current == E.num_rows()) current = 0;
        const std::string line = E.pt.line_text(current);
        size_t match_pos = line.find(query);
        if(match_pos != std::string::npos) {
            last_match = current;
            E.cy = current;
            E.cx = static_cast<int>(match_pos);
            E.row_offset = E.num_rows();
            return;
        }
    }
    editor_set_status_message("Pattern not found: {}", query);
}

void editor_find() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_col_offset = E.col_offset;
    int saved_row_offset = E.row_offset;
    std::string query = editor_prompt("Search: ", editor_find_callback);
    set_mode(EditorMode::NORMAL);
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.col_offset = saved_col_offset;
    E.row_offset = saved_row_offset;
}

// double buffer
struct Abuf {
    std::string b;
};
#define ABUF_INIT {"", 0}

void ab_append(Abuf* ab, const std::string_view s) { ab->b.append(s); }

// output
void editor_scroll() {
    E.rx = cx_to_rx(E.pt.line_text(E.cy), E.cx);
    if(E.cy < E.row_offset) { E.row_offset = E.cy; }
    if(E.cy >= E.row_offset + E.term_rows) {
        E.row_offset = E.cy - E.term_rows + 1;
    }
    if(E.rx < E.col_offset) { E.col_offset = E.rx; }
    if(E.rx >= E.col_offset + E.term_cols) {
        E.col_offset = E.rx - E.term_cols + 1;
    }
}

void editor_draw_rows() {
    const int total = E.num_rows();
    const int last = std::min(total, E.row_offset + E.term_rows);  // exclusive
    size_t base = 0;
    std::string view_text;
    if(E.row_offset < last) {
        base = E.pt.line_start(static_cast<size_t>(E.row_offset));
        const size_t end = E.pt.line_end(static_cast<size_t>(last - 1));
        view_text = E.pt.text(base, end - base);
    }
    size_t pos = 0;
    for(int y = 0; y < E.term_rows; ++y) {
        const int file_row = y + E.row_offset;
        if(E.pt.size() == 0 && y == 0) {
            const std::string welcome =
                std::format("Winter Editor -- version {}", WINTER_VERSION);
            const int pad =
                std::max(0, (E.term_cols - text_width(welcome)) / 2);
            put_text(pad, y, welcome, E.term_cols, Style{});
        } else if(file_row >= total) {
            put_text(0, y, "~", E.term_cols, Style{});
        } else {
            const size_t nl = view_text.find('\n', pos);
            const size_t len =
                (nl == std::string::npos ? view_text.size() : nl) - pos;
            const std::string_view line(view_text.data() + pos, len);
            const auto& spans = E.syntax.line_spans(base + pos, len);
            put_line(y, line, E.col_offset, E.term_cols, spans);
            pos += len + 1;
        }
    }
}

void editor_draw_status_bar() {
    const int y = E.term_rows;
    const Style bar{.attrs = ATTR_REVERSE};
    for(int x = 0; x < E.term_cols; ++x) scr.at(x, y) = Cell{U' ', 1, bar};
    std::string_view mode = "NORMAL";
    switch(E.mode) {
        case INSERT: mode = "INSERT"; break;
        case SEARCH: mode = "SEARCH"; break;
        case NORMAL: mode = "NORMAL"; break;
        case VISUAL: mode = "VISUAL"; break;
        case COMMAND: mode = "COMMAND"; break;
    }
    std::string status =
        std::format("{:.20}/ - {} lines {}",
                    E.filename.empty() ? "[No Name]" : E.filename,
                    E.num_rows(),
                    E.pt.modified() ? "[+]" : "");
    std::string rstatus =
        std::format("{:.20} - {}/{}", mode, E.cy + 1, E.num_rows());
    const int left_end = put_text(0, y, status, E.term_cols, bar);
    const int rw = text_width(rstatus);
    if(E.term_cols - rw >= left_end)
        put_text(E.term_cols - rw, y, rstatus, E.term_cols, bar);
}

void editor_draw_message_bar() {
    if(!E.status_message.empty() && time(nullptr) - E.status_message_time < 5)
        put_text(0, E.term_rows + 1, E.status_message, E.term_cols, Style{});
}

void editor_handle_resize() {
    g_resized = 0;
    int rows, cols;
    if(query_window_size(&rows, &cols) == -1) return;
    rows = std::max(rows, 3);
    cols = std::max(cols, 1);
    E.term_rows = rows - 2;
    E.term_cols = cols;
    scr.resize(cols, rows);
    if(g_scroll) scr.set_scroll_rows(E.term_rows);
}

void editor_refresh_screen() {
    if(g_resized) editor_handle_resize();
    E.syntax.update(E.pt, E.eol);
    editor_scroll();
    scr.clear();
    editor_draw_rows();
    editor_draw_status_bar();
    editor_draw_message_bar();
    scr.cursor(E.rx - E.col_offset, E.cy - E.row_offset);
    scr.flush();
}

// pack this eventually
template <typename... Args>
void editor_set_status_message(std::format_string<Args...> fmt,
                               Args&&... args) {
    E.status_message = std::format(fmt, std::forward<Args>(args)...);
    E.status_message_time = time(nullptr);
}

// input
std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int)) {
    std::string buf;
    while(1) {
        E.status_message = std::format("{}{}", prompt, buf);
        E.status_message_time = time(nullptr);
        editor_refresh_screen();
        int c = editor_read_key();
        if(c == NONE) continue;
        if(c == DELETE || c == CTRL_KEY('h') || c == BACKSPACE) {
            if(!buf.empty()) buf.pop_back();
        } else if(c == '\x1b') {
            editor_set_status_message("");
            if(callback) callback(buf.c_str(), c);
            return "";
        } else if(c == '\r') {
            if(!buf.empty()) {
                editor_set_status_message("");
                if(callback) callback(buf, c);
                return buf;
            }
        } else if(!iscntrl(c) && c < 128) {
            buf.push_back(static_cast<char>(c));
        }
        if(callback) callback(buf, c);
    }
}

void move_vertical(int dir, int count);

void editor_move_cursor(int key) {
    if(key == ARROW_UP || key == 'k') return move_vertical(-1, 1);
    if(key == ARROW_DOWN || key == 'j') return move_vertical(+1, 1);
    const int last = E.num_rows() - 1;
    std::string line = E.pt.line_text(E.cy);
    switch(key) {
        case 'h':
        case ARROW_LEFT:
            if(E.cx > 0) {
                E.cx = static_cast<int>(utf8_prev(line, E.cx));
            } else if(E.cy > 0) {
                E.cy--;
                E.cx = static_cast<int>(line_len(E.cy));
            }
            break;
        case 'l':
        case ARROW_RIGHT:
            if(E.cx < static_cast<int>(line.size())) {
                E.cx = static_cast<int>(utf8_next(line, E.cx));
            } else if(E.cy < last) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case 'k':
        case ARROW_UP:
            if(E.cy > 0) E.cy--;
            break;
        case 'j':
        case ARROW_DOWN:
            if(E.cy < last) E.cy++;
            break;
    }
    line = E.pt.line_text(E.cy);
    E.cx = snap_cx(line, std::min(E.cx, static_cast<int>(line.size())));
    E.want_rx = cx_to_rx(line, E.cx);
}

static bool write_all(int fd, std::string_view s) {
    while(!s.empty()) {
        const ssize_t n = ::write(fd, s.data(), s.size());
        if(n < 0) {
            if(errno == EINTR) continue;
            return false;
        }
        s.remove_prefix(static_cast<size_t>(n));
    }
    return true;
}

int editor_write_file(const std::string& path) {
    const std::string tmp = path + ".winter.tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd == -1) return errno;
    struct stat st;
    if(stat(path.c_str(), &st) == 0) fchmod(fd, st.st_mode & 07777);
    int err = 0;
    E.pt.for_each_chunk(0, E.pt.size(), [&](std::string_view chunk) {
        if(!err && !write_all(fd, chunk)) err = errno;
    });
    if(!err && E.eol && (E.pt.size() > 0 || !E.orig.empty()) &&
       !write_all(fd, "\n"))
        err = errno;
    if(!err && fsync(fd) == -1) err = errno;
    if(close(fd) == -1 && !err) err = errno;
    if(!err && rename(tmp.c_str(), path.c_str()) == -1) err = errno;
    if(err) unlink(tmp.c_str());
    return err;
}

void editor_save() {
    if(E.filename == "") {
        E.filename = editor_prompt("Save As: ", nullptr);
        if(E.filename == "") {
            editor_set_status_message("Save Aborted");
            return;
        }
    }
    const size_t len = E.pt.size() + (E.eol ? 1 : 0);
    if(const int err = editor_write_file(E.filename)) {
        editor_set_status_message("Can't save! IO error: {}", strerror(err));
        return;
    }
    E.pt.mark_saved();
    editor_set_status_message("{} bytes written to disk", len);
}

void editor_command_palette(std::string_view cmd) {
    while(cmd.starts_with(' ')) cmd.remove_prefix(1);
    while(cmd.ends_with(' ')) cmd.remove_suffix(1);
    if(cmd == "q" || cmd == "quit") {
        if(E.pt.modified()) {
            editor_set_status_message(
                "[WARNING]!! File has unsaved changes, q! | quit! to quit "
                "without saving.");
            return;
        }
        editor_quit();
    } else if(cmd == "q!" || cmd == "quit!") {
        editor_quit();
    } else if(cmd == "w" || cmd == "write") {
        editor_save();
    } else if(cmd == "wq" || cmd == "x") {
        editor_save();
        if(!E.pt.modified()) editor_quit();
    } else {
        editor_set_status_message("Unknown Command: {}", cmd);
    }
}

void editor_command_prompt() {
    std::string cmd = editor_prompt(":", nullptr);
    set_mode(EditorMode::NORMAL);
    if(cmd == "") return;
    editor_command_palette(cmd);
    cmd.clear();
}

static void update_want() { E.want_rx = cx_to_rx(E.pt.line_text(E.cy), E.cx); }

void move_vertical(int dir, int count) {
    const int target =
        std::clamp(E.cy + dir * std::max(count, 1), 0, E.num_rows() - 1);
    if(target == E.cy) return;
    E.cy = target;
    const std::string line = E.pt.line_text(E.cy);
    const int size = static_cast<int>(line.size());
    int cx = E.want_rx == INT_MAX ? size : rx_to_cx(line, E.want_rx);
    if(cx >= size && size > 0) cx = static_cast<int>(utf8_prev(line, size));
    E.cx = cx;
}

static void apply_motion(const Target& t) {
    if(!t.ok) return;
    set_cursor_offset(t.pos);
    if(t.to_eol) E.want_rx = INT_MAX;
    else update_want();
}

static size_t utf8_seq_len(char lead) {
    const auto b = static_cast<unsigned char>(lead);
    return b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
}

struct FindMemory {
    bool valid = false;
    motion::Find kind = motion::Find::f;
    std::string ch;
};
static FindMemory last_find;

struct NormalState {
    int count = 0;
    std::string keys;
    bool want_char = false;
    motion::Find find = motion::Find::f;
    std::string arg;
    bool shown = false;

    void reset() {
        count = 0;
        keys.clear();
        want_char = false;
        arg.clear();
        if(shown) E.status_message.clear();
        shown = false;
    }
};
static NormalState N;

static const std::map<std::string, motion::Fn> kMotions = {
    {"h", motion::left},
    {"l", motion::right},
    {"0", motion::to_line_start},
    {"^", motion::to_first_nonblank},
    {"$", motion::to_line_end},
    {"w", motion::next_word},
    {"W", motion::next_WORD},
    {"b", motion::prev_word},
    {"B", motion::prev_WORD},
    {"e", motion::end_word},
    {"E", motion::end_WORD},
    {"gg", motion::to_file_start},
    {"G", motion::to_file_end},
    {"{", motion::para_back},
    {"}", motion::para_fwd},
    {"%", motion::match_pair},
};

using CmdFn = void (*)(int count);
static const std::map<std::string, CmdFn> kCommands = {
    {"j", +[](int n) { move_vertical(+1, n); }},
    {"k", +[](int n) { move_vertical(-1, n); }},
};

template <class M>
static bool has_longer(const M& m, const std::string& keys) {
    const auto it = m.lower_bound(keys);
    return it != m.end() && it->first.starts_with(keys);
}

static void show_pending() {
    editor_set_status_message(
        "{}{}", N.count ? std::to_string(N.count) : std::string(), N.keys);
    N.shown = true;
}

static void do_find(motion::Find k, const std::string& ch, int count,
                    bool repeat) {
    apply_motion(
        motion::find_char(E.pt, cursor_offset(), count, k, ch, repeat));
}

bool normal_feed(int c) {
    if(c == '\x1b') {
        N.reset();
        return true;
    }
    if(c >= 0x100) {
        N.reset();
        return false;
    }
    if(N.want_char) {
        if(N.arg.empty() && c < 32) {
            N.reset();
            return true;
        }
        N.arg += static_cast<char>(c);
        if(N.arg.size() < utf8_seq_len(N.arg[0])) return true;
        last_find = {true, N.find, N.arg};
        do_find(N.find, N.arg, N.count, false);
        N.reset();
        return true;
    }
    if(N.keys.empty() &&
       ((c >= '1' && c <= '9') || (c == '0' && N.count > 0))) {
        N.count = std::min(N.count * 10 + (c - '0'), 1000000);
        show_pending();
        return true;
    }
    N.keys += static_cast<char>(c);
    if(const auto m = kMotions.find(N.keys); m != kMotions.end()) {
        apply_motion(m->second(E.pt, cursor_offset(), N.count));
        N.reset();
        return true;
    }
    if(const auto m = kCommands.find(N.keys); m != kCommands.end()) {
        m->second(N.count);
        N.reset();
        return true;
    }
    if(N.keys.size() == 1) {
        switch(c) {
            case 'f':
                N.find = motion::Find::f;
                N.want_char = true;
                break;
            case 'F':
                N.find = motion::Find::F;
                N.want_char = true;
                break;
            case 't':
                N.find = motion::Find::t;
                N.want_char = true;
                break;
            case 'T':
                N.find = motion::Find::T;
                N.want_char = true;
                break;
            case ';':
            case ',':
                if(last_find.valid) {
                    const auto k = c == ',' ? motion::flip(last_find.kind)
                                            : last_find.kind;
                    do_find(k, last_find.ch, N.count, true);
                }
                N.reset();
                return true;
            default: break;
        }
        if(N.want_char) {
            show_pending();
            return true;
        }
    }
    if(has_longer(kMotions, N.keys) || has_longer(kCommands, N.keys)) {
        show_pending();
        return true;
    }
    if(N.keys.size() == 1) {
        N.keys.clear();
        return false;
    }
    N.reset();
    return true;
}

static void enter_insert_at(int cx) {
    E.cx = cx;
    set_mode(EditorMode::INSERT);
}

void editor_process_key() {
    static int quit_times = WINTER_QUIT_TIMES;
    if(E.mode == EditorMode::COMMAND) {
        editor_command_prompt();
        return;
    } else if(E.mode == EditorMode::SEARCH) {
        editor_find();
        return;
    }
    int c = editor_read_key();
    if(c == NONE) return;
    switch(c) {
        case CTRL_KEY('q'):
            if(E.pt.modified() && quit_times > 0) {
                editor_set_status_message(
                    "[WARNING]!! File has unsaved changes, press CTRL-Q {} "
                    "more times to quit.",
                    quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'): editor_save(); break;

        case HOME: E.cx = 0; break;
        case END:
            if(E.cy < E.num_rows()) E.cx = static_cast<int>(line_len(E.cy));
            break;

        case PAGE_UP:
        case PAGE_DOWN: {
            if(c == PAGE_UP || c == 'g') {
                E.cy = E.row_offset;
            } else if(c == PAGE_DOWN || c == 'G') {
                E.cy = E.row_offset + E.term_rows - 1;
                if(E.cy > E.num_rows() - 1) E.cy = E.num_rows() - 1;
            }
            int times = E.term_rows;
            while(times--) {
                editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
        } break;

        case ARROW_UP: editor_move_cursor(c); break;
        case ARROW_DOWN: editor_move_cursor(c); break;
        case ARROW_LEFT: editor_move_cursor(c); break;
        case ARROW_RIGHT: editor_move_cursor(c); break;

        case CTRL_KEY('l'):
        case '\x1b': {
            const bool was_insert = E.mode == EditorMode::INSERT;
            set_mode(EditorMode::NORMAL);
            if(was_insert && E.cx > 0)
                E.cx = static_cast<int>(utf8_prev(E.pt.line_text(E.cy), E.cx));
            update_want();
        } break;
    }
    switch(E.mode) {
        case INSERT:
            switch(c) {
                case '\r': editor_insert_newline(); break;
                case BACKSPACE: editor_delete_char(); break;
                case DELETE: editor_delete_forward(); break;
                case ARROW_UP:
                case ARROW_DOWN:
                case ARROW_LEFT:
                case ARROW_RIGHT:
                case HOME:
                case END:
                case PAGE_UP:
                case PAGE_DOWN:
                    E.pt.end_group();
                    E.pt.begin_group();
                    break;
                default:
                    if(c >= 32 && c < 127) editor_type_char(c);
                    else if(c == '\t') editor_insert_tab();
                    else if(c >= 0x80 && c < 0x100) editor_insert_char(c);
                    break;
            }
            break;
        case NORMAL:
            if(normal_feed(c)) break;
            switch(c) {
                    // search functions
                case 'n':
                case 'N':
                    editor_find_callback("", c);
                    break;

                    // insert/insert first char of line
                case 'i': set_mode(EditorMode::INSERT); break;
                case 'I': {
                    const std::string line = E.pt.line_text(E.cy);
                    size_t i = 0;
                    while(i < line.size() &&
                          (line[i] == ' ' || line[i] == '\t'))
                        ++i;
                    enter_insert_at(static_cast<int>(i));
                } break;

                    // after curs/eol
                case 'a': {
                    const std::string line = E.pt.line_text(E.cy);
                    const int size = static_cast<int>(line.size());
                    enter_insert_at(
                        E.cx < size ? static_cast<int>(utf8_next(line, E.cx))
                                    : E.cx);
                } break;
                case 'A':
                    enter_insert_at(static_cast<int>(line_len(E.cy)));
                    break;

                    // new line u/d
                case 'o':
                    set_mode(EditorMode::INSERT);
                    editor_open_below();
                    break;
                case 'O':
                    set_mode(EditorMode::INSERT);
                    editor_open_above();
                    break;

                    // undo
                case 'u':
                    if(auto at = E.pt.undo()) set_cursor_offset(*at);
                    else editor_set_status_message("Already at oldest change");
                    break;
                case CTRL_KEY('r'):
                    if(auto at = E.pt.redo()) set_cursor_offset(*at);
                    else editor_set_status_message("Already at newest change");
                    break;

                    // modes
                case '/': set_mode(EditorMode::SEARCH); break;
                case 'v': set_mode(EditorMode::VISUAL); break;
                case ':': set_mode(EditorMode::COMMAND); break;
            }
            N.reset();
            break;
        case VISUAL: break;
        case COMMAND: break;
        case SEARCH: break;
    }
    quit_times = WINTER_QUIT_TIMES;
}

// init
void init_editor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.filename = "";
    E.status_message[0] = '\0';
    E.status_message_time = 0;
    if(get_window_size(&E.term_rows, &E.term_cols) == -1)
        die("get_window_size");
    E.term_rows -= 2;
    scr.resize(E.term_cols, E.term_rows + 2);
    const char* t = std::getenv("TERM");
    g_scroll = !t || std::string_view(t) != "linux";
    if(g_scroll) scr.set_scroll_rows(E.term_rows);
}

int main(int argc, char* argv[]) {
    enable_raw_mode();
    init_editor();
    install_winch_handler();
    if(argc >= 2) { editor_open(argv[1]); }
    editor_set_status_message(
        "HELP: CTRL-S/:w = save | CTRL-Q/:q = quit | / = search");
    if(auto s = E.syntax.summary(); !s.empty())
        editor_set_status_message("{}", s);
    while(1) {
        editor_refresh_screen();
        editor_process_key();
    }
    return 0;
}
