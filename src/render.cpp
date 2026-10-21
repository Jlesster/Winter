#include "render.hpp"

#include <algorithm>
#include <ctime>
#include <string_view>
#include <vector>

#include "buffer.hpp"
#include "columns.hpp"
#include "editor.hpp"
#include "gutter.hpp"
#include "terminal.hpp"
#include "utf8.hpp"

namespace {

Screen scr;

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

void put_line(int y, std::string_view line, int col_off, int cols, int x0,
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
            const int x = x0 + rx - col_off;
            if(cp == '\t' || rx < col_off || next > end) {
                for(int k = std::max(rx, col_off); k < std::min(next, end); k++)
                    scr.at(x0 + k - col_off, y) = Cell{};
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

// output
int gutter() {
    return std::min(gutter_width(E.num_rows()), std::max(E.term_cols - 1, 0));
}
int text_cols() { return std::max(E.term_cols - gutter(), 1); }

void editor_scroll() {
    E.rx = cx_to_rx(E.pt.line_text(E.cy), E.cx);
    if(E.cy < E.row_offset) { E.row_offset = E.cy; }
    if(E.cy >= E.row_offset + E.term_rows) {
        E.row_offset = E.cy - E.term_rows + 1;
    }
    if(E.rx < E.col_offset) { E.col_offset = E.rx; }
    if(E.rx >= E.col_offset + text_cols()) {
        E.col_offset = E.rx - text_cols() + 1;
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
    const int g = gutter();
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
            if(g > 0) {
                const auto attr = file_row == E.cy ? ATTR_BOLD : ATTR_DIM;
                put_text(0,
                         y,
                         gutter_label(file_row, E.cy, g),
                         g,
                         Style{.attrs = static_cast<uint16_t>(attr)});
            }
            put_line(y, line, E.col_offset, text_cols(), g, spans);
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

}  // namespace

void render_resize(int rows, int cols) {
    rows = std::max(rows, 3);  // status + message bar + 1 text row
    cols = std::max(cols, 1);
    E.term_rows = rows - 2;
    E.term_cols = cols;
    scr.resize(cols, rows);
    if(term_caps().scroll_region) scr.set_scroll_rows(E.term_rows);
}

void editor_refresh_screen() {
    if(resize_pending()) {
        resize_ack();  // clear first: a resize during the query re-arms it
        int rows, cols;
        if(query_window_size(&rows, &cols) == 0) render_resize(rows, cols);
    }
    E.syntax.update(E.pt, E.eol);
    editor_scroll();
    scr.clear();
    editor_draw_rows();
    editor_draw_status_bar();
    editor_draw_message_bar();
    scr.cursor(gutter() + E.rx - E.col_offset, E.cy - E.row_offset);
    set_cursor_for_mode(E.mode);
    scr.flush();
}
