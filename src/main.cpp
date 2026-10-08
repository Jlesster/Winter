#include <trux/core.hpp>
#include "core.hpp"

#include <cctype>
#include <cstdarg>
#include <ctime>

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
#include <trux/terminal/ansi.hpp>

struct ERow {
    int idx;
    int size;
    int real_size;
    char* chars;
    char* render;
    unsigned char* highlight;
    int hl_open_comment;
};

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
};

// data
struct EditorSyntax {
    const char* filetype;
    const char** filematch;
    const char** keywords;
    const char* singleline_comment_start;
    const char* multiline_comment_start;
    const char* multiline_comment_end;
    int flags;
};

struct EditorConfig {
    bool dirty;
    int cx, cy;
    int rx;
    int row_offset;
    int col_offset;
    int term_rows;
    int term_cols;
    int num_rows;
    int dirt_amount;
    ERow* row;
    char* filename;
    char status_message[80];
    time_t status_message_time;
    termios orig_termios;
    EditorSyntax* syntax;
    EditorMode mode = EditorMode::NORMAL;
};

EditorConfig E;

// filetypes
const char* C_HL_EXTENSIONS[] = {".c", ".h", ".cpp", ".hpp", nullptr};
const char* C_HL_KEYWORDS[] = {
    "switch", "if",    "while",     "for",     "break",   "continue",
    "return", "else",  "struct",    "union",   "typedef", "static",
    "enum",   "class", "case",      "int|",    "long|",   "double|",
    "float|", "char|", "unsigned|", "signed|", "void|",   nullptr};

struct EditorSyntax HLDB[] = {
    {"c",
     C_HL_EXTENSIONS,
     C_HL_KEYWORDS,
     "//",
     "/*",
     "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

void editor_refresh_screen();
char* editor_prompt(const char* prompt, void (*callback)(char*, int));
void editor_set_status_message(const char* fmt, ...);

// terminal
void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
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
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    if(c == '\x1b') {
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if(seq[0] == '[') {
            if(seq[1] >= '0' && seq[1] <= '9') {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '1': return HOME;
                        case '3': return DELETE;
                        case '4': return END;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME;
                        case '8': return END;
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'F': return HOME;
                    case 'H': return END;
                }
            }
        } else if(seq[0] == '0') {
            switch(seq[1]) {
                case 'H': return HOME;
                case 'F': return END;
            }
        }
        return '\x1b';
    } else return c;
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

// row operation
int editor_row_cx_to_rx(ERow* row, int cx) {
    int rx = 0;
    int j;
    for(j = 0; j < cx; j++) {
        if(row->chars[j] == '\t')
            rx += (WINTER_TAB_STOP - 1) - (rx % WINTER_TAB_STOP);
        rx++;
    }
    return rx;
}

int editor_row_rx_to_cx(ERow* row, int rx) {
    int cursor_rx = 0;
    int cx;
    for(cx = 0; cx < row->size; cx++) {
        if(row->chars[cx] == '\t')
            cursor_rx += (WINTER_TAB_STOP - 1) - (cursor_rx % WINTER_TAB_STOP);
        cursor_rx++;
        if(cursor_rx > rx) return cx;
    }
    return cx;
}

void editor_update_row(ERow* row) {
    int tabs = 0;
    int j;
    for(j = 0; j < row->size; j++) {
        if(row->chars[j] == '\t') tabs++;
    }
    free(row->render);
    row->render = static_cast<char*>(
        malloc(row->size + tabs * (WINTER_TAB_STOP - 1) + 1));
    int idx = 0;
    for(j = 0; j < row->size; j++) {
        if(row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while(idx % WINTER_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->real_size = idx;
}

void editor_insert_row(int at, const char* s, size_t len) {
    if(at < 0 || at > E.num_rows) return;
    E.row = static_cast<ERow*>(realloc(E.row, sizeof(ERow) * (E.num_rows + 1)));
    memmove(&E.row[at + 1], &E.row[at], sizeof(ERow) * (E.num_rows - at));
    for(int j = at + 1; j <= E.num_rows; j++) E.row[j].idx++;
    E.row[at].idx = at;
    E.row[at].size = len;
    E.row[at].chars = static_cast<char*>(malloc(len + 1));
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].real_size = 0;
    E.row[at].render = nullptr;
    E.row[at].highlight = nullptr;
    E.row[at].hl_open_comment = 0;
    editor_update_row(&E.row[at]);
    E.num_rows++;
    E.dirty = true;
}

void editor_free_row(ERow* row) {
    free(row->render);
    free(row->chars);
    free(row->highlight);
}

void editor_delete_row(int at) {
    if(at < 0 || at >= E.num_rows) return;
    editor_free_row(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(ERow) * (E.num_rows - at - 1));
    for(int j = at; j < E.num_rows - 1; j++) E.row[j].idx--;
    E.num_rows--;
    E.dirty = true;
    E.dirt_amount++;
}

void editor_row_insert_char(ERow* row, int at, int c) {
    if(at < 0 || at > row->size) at = row->size;
    row->chars = static_cast<char*>(realloc(row->chars, row->size + 2));
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editor_update_row(row);
}

void editor_row_append_string(ERow* row, char* s, size_t len) {
    row->chars = static_cast<char*>(realloc(row->chars, row->size + len + 1));
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
    E.dirty = true;
    E.dirt_amount++;
}

void editor_row_delete_char(ERow* row, int at) {
    if(at < 0 || at > row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editor_update_row(row);
    E.dirty = true;
    E.dirt_amount++;
}

void editor_insert_char(int c) {
    if(E.cy == E.num_rows) { editor_insert_row(E.num_rows, "", 0); }
    editor_row_insert_char(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editor_insert_newline() {
    if(E.cx == 0) {
        editor_insert_row(E.cy, "", 0);
    } else {
        ERow* row = &E.row[E.cy];
        editor_insert_row(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editor_update_row(row);
    }
    E.cy++;
    E.cx = 0;
}

void editor_delete_char() {
    if(E.cy == E.num_rows) return;
    if(E.cx == 0 && E.cy == 0) return;
    ERow* row = &E.row[E.cy];
    if(E.cx > 0) {
        editor_row_delete_char(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editor_row_append_string(&E.row[E.cy - 1], row->chars, row->size);
        editor_delete_row(E.cy);
        E.cy--;
    }
}

// file io
char* editor_rows_to_string(int* buf_len) {
    int total_len = 0;
    int j;
    for(j = 0; j < E.num_rows; j++) total_len += E.row[j].size + 1;
    *buf_len = total_len;
    char* buf = static_cast<char*>(malloc(total_len));
    char* p = buf;
    for(j = 0; j < E.num_rows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editor_open(char* filename) {
    free(E.filename);
    E.filename = strdup(filename);
    FILE* file_pointer = fopen(filename, "r");
    if(!file_pointer) die("fopen");
    char* line = nullptr;
    size_t line_cap = 0;
    ssize_t line_len;
    while((line_len = getline(&line, &line_cap, file_pointer)) != -1) {
        while(line_len > 0 &&
              (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
            line_len--;
        editor_insert_row(E.num_rows, line, line_len);
    }
    free(line);
    fclose(file_pointer);
    E.dirty = false;
    E.dirt_amount = 0;
}

// find

void editor_find_callback(char* query, int key) {
    static int last_match = -1;
    static char* saved_query = nullptr;
    static int saved_highlight_line;
    static char* saved_highlight = nullptr;
    int direction = 1;
    if(saved_highlight) {
        memcpy(E.row[saved_highlight_line].highlight,
               saved_highlight,
               E.row[saved_highlight_line].real_size);
        free(saved_highlight);
        saved_highlight = nullptr;
    }
    if(E.mode == EditorMode::NORMAL && (key == 'n' || key == 'N')) {
        if(!saved_query) {
            editor_set_status_message("No previous search");
            return;
        }
        query = saved_query;
        direction = (key == 'N') ? -1 : 1;
    } else if(key == '\r') {
        free(saved_query);
        saved_query = strdup(query);
        return;
    } else {
        last_match = -1;
        if(key == '\x1b' || query[0] == '\0') return;
    }
    if(last_match == -1) direction = 1;
    int current = last_match;
    int i;
    for(i = 0; i < E.num_rows; i++) {
        current += direction;
        if(current == -1) current = E.num_rows - 1;
        else if(current == E.num_rows) current = 0;
        ERow* row = &E.row[current];
        char* match = strstr(row->render, query);
        if(match) {
            last_match = current;
            E.cy = current;
            E.cx = editor_row_rx_to_cx(row, match - row->render);
            E.row_offset = E.num_rows;
            saved_highlight_line = current;
            saved_highlight = static_cast<char*>(malloc(row->real_size));
            memcpy(saved_highlight, row->highlight, row->real_size);
            return;
        }
    }
    editor_set_status_message("Pattern not found: %s", query);
}

void editor_find() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_col_offset = E.col_offset;
    int saved_row_offset = E.row_offset;
    char* query =
        editor_prompt("Search: %s (ESC to cancel)", editor_find_callback);
    E.mode = EditorMode::NORMAL;
    if(query) {
        free(query);
    } else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.col_offset = saved_col_offset;
        E.row_offset = saved_row_offset;
    }
}

// double buffer
struct Abuf {
    char* b;
    int len;
};
#define ABUF_INIT {nullptr, 0}

void ab_append(Abuf* ab, const char* s, int len) {
    char* new_buf = static_cast<char*>(realloc(ab->b, ab->len + len));
    if(new_buf == nullptr) return;
    memcpy(&new_buf[ab->len], s, len);
    ab->b = new_buf;
    ab->len += len;
}

void ab_free(Abuf* ab) { free(ab->b); }

// output
void editor_scroll() {
    E.rx = 0;
    if(E.cy < E.num_rows) { E.rx = editor_row_cx_to_rx(&E.row[E.cy], E.cx); }
    if(E.cy < E.row_offset) { E.row_offset = E.cy; }
    if(E.cy >= E.row_offset + E.term_rows) {
        E.row_offset = E.cy - E.term_rows + 1;
    }
    if(E.rx < E.col_offset) { E.col_offset = E.rx; }
    if(E.rx >= E.col_offset + E.term_cols) {
        E.col_offset = E.rx - E.term_cols + 1;
    }
}

void editor_draw_rows(Abuf* ab) {
    int y;
    for(y = 0; y < E.term_rows; ++y) {
        // xy from top left
        int file_row = y + E.row_offset;
        if(file_row >= E.num_rows) {
            if(E.num_rows == 0 && y == 0) {
                char welcome[80];
                int welcome_len = snprintf(welcome,
                                           sizeof(welcome),
                                           "Winter editor -- version %s",
                                           WINTER_VERSION);
                if(welcome_len > E.term_cols) welcome_len = E.term_cols;
                int padding = (E.term_cols - welcome_len) / 2;
                if(padding) {
                    ab_append(ab, " ", 1);
                    padding--;
                }
                while(padding--) { ab_append(ab, " ", 1); }
                ab_append(ab, welcome, welcome_len);
            } else {
                ab_append(ab, "~", 1);
            }
        } else {
            int len = E.row[file_row].real_size - E.col_offset;
            if(len < 0) len = 0;
            if(len > E.term_cols) len = E.term_cols;
            char* c = &E.row[file_row].render[E.col_offset];
            int current_color = -1;
            int j;
            for(j = 0; j < len; j++) {
                if(iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    ab_append(ab, "\x1b[7m", 4);
                    ab_append(ab, &sym, 1);
                    ab_append(ab, "\x1b[m", 3);
                    if(current_color != -1) {
                        char buf[16];
                        int char_len = snprintf(
                            buf, sizeof(buf), "\x1b[%dm", current_color);
                        ab_append(ab, buf, char_len);
                    }
                } else {
                    ab_append(ab, &c[j], 1);
                }
            }
            ab_append(ab, "\x1b[39m", 5);
        }
        ab_append(ab, "\x1b[K", 3);
        ab_append(ab, "\r\n", 2);
    }
}

void editor_draw_status_bar(Abuf* ab) {
    ab_append(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    const char* mode;
    switch(E.mode) {
        case INSERT: mode = "INSERT"; break;
        case SEARCH: mode = "SEARCH"; break;
        case NORMAL: mode = "NORMAL"; break;
        case VISUAL: mode = "VISUAL"; break;
        case COMMAND: mode = "COMMAND"; break;
    }
    int len = snprintf(status,
                       sizeof(status),
                       "%.20s/%s - %d lines %s",
                       E.filename ? E.filename : "[No Name]",
                       E.syntax ? E.syntax->filetype : "No FT",
                       E.num_rows,
                       E.dirty ? "[+]" : "");
    int rlen = snprintf(
        rstatus, sizeof(rstatus), "%.20s - %d/%d", mode, E.cy + 1, E.num_rows);
    if(len > E.term_cols) len = E.term_cols;
    ab_append(ab, status, len);
    while(len < E.term_cols) {
        if(E.term_cols - len == rlen) {
            ab_append(ab, rstatus, rlen);
            break;
        } else {
            ab_append(ab, " ", 1);
            len++;
        }
    }
    ab_append(ab, "\x1b[m", 3);
    ab_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(Abuf* ab) {
    ab_append(ab, "\x1b[K", 3);
    int msg_len = strlen(E.status_message);
    if(msg_len > E.term_cols) msg_len = E.term_cols;
    if(msg_len && time(nullptr) - E.status_message_time < 5)
        ab_append(ab, E.status_message, msg_len);
}

void editor_refresh_screen() {
    editor_scroll();
    Abuf ab = ABUF_INIT;
    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H", 3);
    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);
    char buf[32];
    snprintf(buf,
             sizeof(buf),
             "\x1b[%d;%dH",
             (E.cy - E.row_offset) + 1,
             (E.rx - E.col_offset) + 1);
    ab_append(&ab, buf, strlen(buf));
    ab_append(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

// pack this eventually
void editor_set_status_message(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_message, sizeof(E.status_message), fmt, ap);
    va_end(ap);
    E.status_message_time = time(nullptr);
}

// input
char* editor_prompt(const char* prompt, void (*callback)(char*, int)) {
    size_t buf_size = 128;
    char* buf = static_cast<char*>(malloc(buf_size));
    size_t buf_len = 0;
    buf[0] = '\0';
    while(1) {
        editor_set_status_message(prompt, buf);
        editor_refresh_screen();
        int c = editor_read_key();
        if(c == DELETE || c == CTRL_KEY('h') || c == BACKSPACE) {
            if(buf_len != 0) buf[--buf_len] = '\0';
        } else if(c == '\x1b') {
            editor_set_status_message("");
            if(callback) callback(buf, c);
            free(buf);
            return nullptr;
        } else if(c == '\r') {
            if(buf_len != 0) {
                editor_set_status_message("");
                if(callback) callback(buf, c);
                return buf;
            }
        } else if(!iscntrl(c) && c < 128) {
            if(buf_len == buf_size - 1) {
                buf_size *= 2;
                buf = static_cast<char*>(realloc(buf, buf_size));
            }
            buf[buf_len++] = c;
            buf[buf_len] = '\0';
        }
        if(callback) callback(buf, c);
    }
}

void editor_move_cursor(int key) {
    ERow* row = (E.cy >= E.num_rows) ? nullptr : &E.row[E.cy];
    switch(key) {
        case 'h':
        case ARROW_LEFT:
            if(E.cx != 0) {
                E.cx--;
            } else if(E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case 'l':
        case ARROW_RIGHT:
            if(row && E.cx < row->size) {
                E.cx++;
            } else if(row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case 'k':
        case ARROW_UP:
            if(E.cy != 0) E.cy--;
            break;
        case 'j':
        case ARROW_DOWN:
            if(E.cy < E.num_rows) E.cy++;
            break;
    }
    row = (E.cy >= E.num_rows) ? nullptr : &E.row[E.cy];
    int row_len = row ? row->size : 0;
    if(E.cx > row_len) { E.cx = row_len; }
}

void editor_save() {
    if(E.filename == nullptr) {
        E.filename = editor_prompt("Save as: %s", nullptr);
        if(E.filename == nullptr) {
            editor_set_status_message("Save aborted");
            return;
        }
    }
    int len;
    char* buf = editor_rows_to_string(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if(fd != -1) {
        if(ftruncate(fd, len) != -1) {
            if(write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = false;
                E.dirt_amount = 0;
                editor_set_status_message("%d bytes writen to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editor_set_status_message("Can't Save! ~UnU~ I/O erro: %s",
                              strerror(errno));
}

void editor_command_palette(const char* cmd) {
    if(strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        if(E.dirty) {
            editor_set_status_message(
                "[WARNING]!! File has unsaved changes, q! | quit! "
                "to quit without saving.");
            return;
        }
        editor_quit();
    } else if(strcmp(cmd, "q!") == 0 || strcmp(cmd, "quit!") == 0) {
        editor_quit();
    } else if(strcmp(cmd, "w") == 0 || strcmp(cmd, "write") == 0) {
        editor_save();
    } else if(strcmp(cmd, "wq") == 0) {
        editor_save();
        if(!E.dirty) editor_quit();
    } else {
        editor_set_status_message("Unknown Command: %s", cmd);
    }
}

void editor_command_prompt() {
    char* cmd = editor_prompt(":%s", nullptr);
    E.mode = EditorMode::NORMAL;
    if(cmd == nullptr) return;
    editor_command_palette(cmd);
    free(cmd);
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
    switch(c) {
        case CTRL_KEY('q'):
            if(E.dirty && quit_times > 0) {
                editor_set_status_message(
                    "[WARNING]!! File has unsaved changes, press CTRL-Q %d "
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
            if(E.cy < E.num_rows) E.cx = E.row[E.cy].size;
            break;

        case PAGE_UP:
        case PAGE_DOWN: {
            if(c == PAGE_UP || c == 'g') {
                E.cy = E.row_offset;
            } else if(c == PAGE_DOWN || c == 'G') {
                E.cy = E.row_offset + E.term_rows - 1;
                if(E.cy > E.num_rows) E.cy = E.num_rows;
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
        case '\x1b': E.mode = EditorMode::NORMAL; break;
    }
    switch(E.mode) {
        case INSERT:
            switch(c) {
                case '\r': editor_insert_newline(); break;
                case BACKSPACE: editor_delete_char(); break;
                default:
                    if((c >= 32 && c < 127) || c == '\t') editor_insert_char(c);
                    break;
            }
            break;
        case NORMAL:
            switch(c) {
                case 'h': editor_move_cursor(c); break;
                case 'j': editor_move_cursor(c); break;
                case 'k': editor_move_cursor(c); break;
                case 'l': editor_move_cursor(c); break;

                case 'n':
                case 'N': editor_find_callback(nullptr, c); break;

                case '/': E.mode = EditorMode::SEARCH; break;
                case 'v': E.mode = EditorMode::VISUAL; break;
                case 'i': E.mode = EditorMode::INSERT; break;
                case ':': E.mode = EditorMode::COMMAND; break;
            }
            break;
        case VISUAL: break;
        case COMMAND: break;
        case SEARCH: break;
    }
    quit_times = WINTER_QUIT_TIMES;
}

// init
void init_editor() {
    E.dirty = false;
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.num_rows = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.dirt_amount = 0;
    E.row = nullptr;
    E.filename = nullptr;
    E.status_message[0] = '\0';
    E.status_message_time = 0;
    E.syntax = nullptr;
    if(get_window_size(&E.term_rows, &E.term_cols) == -1)
        die("get_window_size");
    E.term_rows -= 2;
}

int main(int argc, char* argv[]) {
    enable_raw_mode();
    init_editor();
    if(argc >= 2) { editor_open(argv[1]); }
    editor_set_status_message(
        "HELP: CTRL-S/:w = save | CTRL-Q/:q = quit | / = search");
    while(1) {
        editor_refresh_screen();
        editor_process_key();
    }
    return 0;
}
