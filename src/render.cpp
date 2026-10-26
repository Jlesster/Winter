#include "render.hpp"

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <string_view>
#include <vector>

#include "buffer.hpp"
#include "columns.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "gutter.hpp"
#include "lsp_sync.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "prompt.hpp"
#include "search.hpp"
#include "terminal.hpp"
#include "utf8.hpp"

namespace {

constexpr uint32_t kSearchBg = 0x585b70;     // every match
constexpr uint32_t kSearchCurBg = 0xf9e2af;  // the one under the cursor
constexpr uint32_t kSearchCurFg = 0x1e1e2e;
constexpr uint32_t kSearchCurCursor = 0x89b4fa;  // cursor block over query

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
              const std::vector<Span>& spans = {}, size_t sel_lo = 0,
              size_t sel_hi = 0, const std::vector<DiagMark>& marks = {},
              const std::vector<SearchMark>& hits = {}) {
    const int end = col_off + cols;
    int rx = 0;
    int last = -1;
    size_t si = 0, mi = 0;
    for(size_t i = 0; i < line.size() && rx < end;) {
        const size_t at = i;
        const Decoded d = utf8_decode(line, i);
        i += d.len;
        while(si < spans.size() && spans[si].end <= at) ++si;
        Style st = (si < spans.size() && spans[si].start <= at) ? spans[si].st
                                                                : Style{};
        while(mi < hits.size() && hits[mi].hi <= at) ++mi;
        if(mi < hits.size() && hits[mi].lo <= at) {
            st.bg = hits[mi].cur ? kSearchCurBg : kSearchBg;
            if(hits[mi].cur) {
                st.fg = kSearchCurFg;
                st.attrs = static_cast<uint16_t>(st.attrs | ATTR_BOLD);
            }
        }
        if(at >= sel_lo && at < sel_hi)
            st.attrs = static_cast<uint16_t>(st.attrs | ATTR_REVERSE);
        if(!marks.empty()) {
            const DiagMark* m = nullptr;
            for(const DiagMark& k : marks)
                if(at >= k.lo && at < k.hi && (!m || k.sev < m->sev)) m = &k;
            if(m) {
                st.attrs = static_cast<uint16_t>(
                    st.attrs |
                    (opts.diag_undercurl ? ATTR_UNDERCURL : ATTR_UNDERLINE));
                st.ul = diag_color(m->sev);
            }
        }
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
                    scr.at(x0 + k - col_off, y) = Cell{U' ', 1, st};
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
    E.rx = cx_to_rx(E.buf().pt.line_text(E.cy), E.cx);
    const int total = E.num_rows();
    const int so = std::min(opts.scrolloff, (E.term_rows - 1) / 2);
    const int bot = std::min(opts.scrolloff, E.term_rows - 1 - so);
    const int pad = opts.scrolleof ? bot : 0;
    int off = E.row_offset;
    if(E.cy < off + so) off = E.cy - so;
    if(E.cy > off + E.term_rows - 1 - bot) off = E.cy - (E.term_rows - 1 - bot);
    E.row_offset = std::clamp(off, 0, std::max(0, total + pad - E.term_rows));
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
        base = E.buf().pt.line_start(static_cast<size_t>(E.row_offset));
        const size_t end = E.buf().pt.line_end(static_cast<size_t>(last - 1));
        view_text = E.buf().pt.text(base, end - base);
    }
    E.buf().syntax.rainbow_prepare(base, base + view_text.size());
    const int g = gutter();
    size_t pos = 0;
    const bool vis = E.mode == VISUAL || (E.mode == COMMAND && E.cmd_sel);
    const Range sel = E.mode == VISUAL ? visual_range() 
                                       : vis ? Range{E.cmd_sel_start, E.cmd_sel_end} 
                                       : Range{};
    const bool dg = opts.diagnostics;
    std::vector<DiagMark> marks;
    std::vector<SearchMark> hits;
    for(int y = 0; y < E.term_rows; ++y) {
        const int file_row = y + E.row_offset;
        if(E.buf().pt.size() == 0 && y == 0) {
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
            const auto& spans = E.buf().syntax.line_spans(base + pos, len);
            if(g > 0) {
                const auto attr = file_row == E.cy ? ATTR_BOLD : ATTR_DIM;
                put_text(0,
                         y,
                         gutter_label(file_row, E.cy, g),
                         g,
                         Style{.attrs = static_cast<uint16_t>(attr)});
            }
            const size_t ls = base + pos;
            size_t lo = 0, hi = 0;
            if(vis && sel.end > ls && sel.start <= ls + len) {
                lo = std::max(sel.start, ls) - ls;
                hi = std::min(sel.end, ls + len) - ls;
                if(len == 0)
                    put_text(
                        g, y, " ", E.term_cols, Style{.attrs = ATTR_REVERSE});
            }
            if(dg) diag_marks(E.buf().diags, ls, len, marks);
            else marks.clear();
            search_marks(file_row, line, hits);
            put_line(y,
                     line,
                     E.col_offset,
                     text_cols(),
                     g,
                     spans,
                     lo,
                     hi,
                     marks,
                     hits);
            if(dg && opts.diag_virtual_text) {
                if(const Diag* w = diag_worst_on_line(E.buf().diags, ls, len)) {
                    const int end_x = g +
                                      cx_to_rx(line, static_cast<int>(len)) -
                                      E.col_offset;
                    const int x = std::max(g + 1, end_x + 2);
                    if(x < E.term_cols - 4)
                        put_text(x,
                                 y,
                                 std::format("■ {}", diag_first_line(*w)),
                                 E.term_cols,
                                 Style{.fg = diag_color(w->severity),
                                       .attrs = ATTR_ITALIC});
                }
            }
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
        case VISUAL: mode = E.v_line ? "V-LINE" : "VISUAL"; break;
        case COMMAND: mode = "COMMAND"; break;
        case OPPEND: break;
    }
    const std::string bufs =
        E.buffers.size() > 1
            ? std::format(" [{}/{}]", E.cur + 1, E.buffers.size())
            : "";
    std::string status =
        std::format("{:.20}/ - {} lines {}{}",
                    E.buf().filename.empty() ? "[No Name]" : E.buf().filename,
                    E.num_rows(),
                    E.buf().pt.modified() ? "[+]" : "",
                    bufs);
    std::string rstatus = std::format(
        "{}{:.20} - {}/{}",
        opts.diagnostics ? diag_summary(E.buf().diags) : std::string{},
        mode,
        E.cy + 1,
        E.num_rows());
    const int left_end = put_text(0, y, status, E.term_cols, bar);
    const int rw = text_width(rstatus);
    if(E.term_cols - rw >= left_end)
        put_text(E.term_cols - rw, y, rstatus, E.term_cols, bar);
}

void editor_draw_message_bar() {
    if(!E.status_message.empty() && time(nullptr) - E.status_message_time < 5) {
        put_text(0, E.term_rows + 1, E.status_message, E.term_cols, Style{});
    } else if(opts.diagnostics) {
        const PieceTable& pt = E.buf().pt;
        const size_t ls = pt.line_start(static_cast<size_t>(E.cy));
        if(const Diag* w = diag_worst_on_line(
               E.buf().diags, ls, pt.line_end(static_cast<size_t>(E.cy)) - ls))
            put_text(0,
                     E.term_rows + 1,
                     std::format(
                         "{}: {}", diag_name(w->severity), diag_first_line(*w)),
                     E.term_cols,
                     Style{.fg = diag_color(w->severity)});
    }
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
    E.buf().syntax.update(E.buf().pt, E.buf().eol);
    lsp_sync_tick();
    editor_scroll();
    scr.clear();
    editor_draw_rows();
    editor_draw_status_bar();
    editor_draw_message_bar();
    const PromptView& pv = prompt_view();
    const int sx = pv.active ? std::min(pv.col, scr.width() - 1)
                             : gutter() + E.rx - E.col_offset;
    const int sy = pv.active ? E.term_rows + 1 : E.cy - E.row_offset;
    scr.cursor(sx, sy);
    set_cursor_for_mode(pv.active ? INSERT : E.mode);
    uint32_t col = 0xFFFFFFFF;
    if(opts.cursor_inverse && sx >= 0 && sx < scr.width() && sy >= 0 &&
       sy < scr.height()) {
        const Style& s = scr.at(sx, sy).style;
        col = s.bg == kSearchCurBg ? kSearchCurCursor : s.fg;
    }
    set_cursor_color(col);
    scr.flush();
}
