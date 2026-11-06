#include "render.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "buffer.hpp"
#include "columns.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "glyphs.hpp"
#include "gutter.hpp"
#include "lsp_sync.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "overlay.hpp"
#include "prompt.hpp"
#include "search.hpp"
#include "snippet.hpp"
#include "statusline.hpp"
#include "src/command.hpp"
#include "terminal.hpp"
#include "theme.hpp"
#include "tree.hpp"
#include "utf8.hpp"
#include "virt.hpp"

namespace {

Screen scr;
int g_px = 0;

int put_text(int x, int y, std::string_view s, int limit, Style st) {
    x += g_px;
    limit += g_px;
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
    return x - g_px;
}

void put_eols(int y, std::string_view line, int g, std::vector<EolItem>& items,
              int col_off) {
    std::ranges::stable_sort(items, std::greater<>{}, &EolItem::prio);
    const int end_x =
        g + cx_to_rx(line, static_cast<int>(line.size())) - col_off;
    int x = std::max(g + 1, end_x + 2);
    for(const EolItem& it : items) {
        if(x >= E.term_cols - 4) break;
        x = put_text(x, y, it.vt->text, E.term_cols, it.vt->style) + 2;
    }
}

void put_line(int y, std::string_view line, int col_off, int cols, int x0,
              const std::vector<Span>& spans = {}, size_t sel_lo = 0,
              size_t sel_hi = 0, const std::vector<LineSpan>& runs = {}) {
    x0 += g_px;
    const Style ctl = theme_ui("ui.ctrl", Style{.attrs = ATTR_REVERSE});
    const Style vis = theme_ui("ui.visual", Style{.attrs = ATTR_REVERSE});
    const int end = col_off + cols;
    int rx = 0;
    int last = -1;
    size_t si = 0, ri = 0;
    for(size_t i = 0; i < line.size() && rx < end;) {
        const size_t at = i;
        const Decoded d = utf8_decode(line, i);
        i += d.len;
        while(si < spans.size() && spans[si].end <= at) ++si;
        Style st = (si < spans.size() && spans[si].start <= at) ? spans[si].st
                                                                : Style{};
        while(ri < runs.size() && runs[ri].hi <= at) ++ri;
        if(ri < runs.size() && runs[ri].lo <= at) style_patch(st, runs[ri].st);
        if(at >= sel_lo && at < sel_hi) style_patch(st, vis);
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
                scr.at(x, y) = Cell{static_cast<char32_t>(shown), 1, ctl};
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

namespace {

// Screen column of the cursor. E.rx counts source columns; on a concealed
// markdown line the cursor sits where its character lands in the drawn text.
int g_crx = 0;

int cursor_rx() {
    const std::string raw = E.buf().pt.line_text(E.cy);
    SubstView v;
    if(!md_line_view(E.cy, raw, text_cols(), v)) return E.rx;
    return cx_to_rx(v.text, snap_cx(v.text, static_cast<int>(v.map(E.cx))));
}

struct RowPos {  // a screen-row position: file line + wrapped segment of it
    int line = 0, sub = 0;
    auto operator<=>(const RowPos&) const = default;
};

// rx where each wrapped segment of `line` starts. A wide char never straddles
// a break; a tab may (put_line clips it per segment). The cursor line gets an
// extra row when the cursor sits just past a full last segment (insert mode).
std::vector<int> wrap_starts(int line, int W) {
    const std::string raw = E.buf().pt.line_text(line);
    SubstView view;  // a concealed markdown line wraps as it is drawn
    const std::string_view text =
        md_line_view(line, raw, W, view) ? std::string_view(view.text)
                                         : std::string_view(raw);
    std::vector<int> st{0};
    int rx = 0, s = 0;
    for(size_t i = 0; i < text.size();) {
        const Decoded d = utf8_decode(text, i);
        i += d.len;
        const int w = cell_width(d.cp, rx);
        if(w == 0) continue;
        if(d.cp == U'\t') {
            while(rx + w > s + W) st.push_back(s += W);
        } else if(rx + w > s + W) {
            st.push_back(s = rx);
        }
        rx += w;
    }
    if(line == E.cy && g_crx >= s + W) st.push_back(s + W);
    return st;
}

int rows_of(int line, int W) {
    return static_cast<int>(wrap_starts(line, W).size());
}

int seg_of(const std::vector<int>& st, int rx) {
    return static_cast<int>(std::ranges::upper_bound(st, rx) - st.begin()) - 1;
}

RowPos step_down(RowPos p, int n, int W) {  // n screen rows forward
    const int total = E.num_rows();
    while(n > 0 && p.line < total) {
        const int left = rows_of(p.line, W) - p.sub;
        if(n < left) {
            p.sub += n;
            return p;
        }
        n -= left;
        ++p.line;
        p.sub = 0;
    }
    return p;
}

RowPos step_up(RowPos p, int n, int W) {  // n screen rows back, clamps at 0
    while(n > 0) {
        if(p.sub > 0) {
            const int k = std::min(n, p.sub);
            p.sub -= k;
            n -= k;
            continue;
        }
        if(p.line == 0) return p;
        --p.line;
        p.sub = rows_of(p.line, W);
    }
    return p;
}

// rows from a to b (a <= b), stopping early once the count reaches `cap`
int rows_between(RowPos a, RowPos b, int W, int cap = INT_MAX) {
    if(a.line == b.line) return b.sub - a.sub;
    int n = rows_of(a.line, W) - a.sub;
    for(int l = a.line + 1; l < b.line && n < cap; ++l) n += rows_of(l, W);
    return n + b.sub;
}

}  // namespace

void scroll_wrapped(int so, int bot, int pad) {
    const int W = text_cols();
    E.col_offset = 0;
    const RowPos cur{E.cy, seg_of(wrap_starts(E.cy, W), g_crx)};
    RowPos top{std::clamp(E.row_offset, 0, E.num_rows() - 1), E.row_sub};
    top.sub = std::clamp(top.sub, 0, rows_of(top.line, W) - 1);

    const RowPos lim = step_up(cur, so, W);  // highest top that keeps `so` rows
    if(top > lim) top = lim;
    const int rmax = E.term_rows - 1 - bot;
    if(rows_between(top, cur, W, rmax + 1) > rmax) top = step_up(cur, rmax, W);
    const RowPos eof = step_up(RowPos{E.num_rows(), 0}, E.term_rows - pad, W);
    if(top > eof) top = eof;
    E.row_offset = top.line;
    E.row_sub = top.sub;
}

void editor_scroll() {
    E.rx = cx_to_rx(E.buf().pt.line_text(E.cy), E.cx);
    g_crx = cursor_rx();
    const int total = E.num_rows();
    const int so = std::min(opts.scrolloff, (E.term_rows - 1) / 2);
    const int bot = std::min(opts.scrolloff, E.term_rows - 1 - so);
    const int pad = opts.scrolleof ? bot : 0;
    int off = E.row_offset;
    if(E.cy < off + so) off = E.cy - so;
    if(E.cy > off + E.term_rows - 1 - bot) off = E.cy - (E.term_rows - 1 - bot);
    E.row_offset = std::clamp(off, 0, std::max(0, total + pad - E.term_rows));
    if(g_crx < E.col_offset) { E.col_offset = g_crx; }
    if(g_crx >= E.col_offset + text_cols()) {
        E.col_offset = g_crx - text_cols() + 1;
    }
}

namespace {
struct ParenPair {
    size_t a = 0, b = 0;
    bool ok = false;
};

ParenPair find_paren_pair() {
    ParenPair out;
    if(!opts.matchparen) return out;
    Buffer& B = E.buf();
    PieceTable& pt = B.pt;
    constexpr std::string_view kPairs = "()[]{}";
    const auto is_br = [&](size_t p) {
        return p < pt.size() && kPairs.find(pt.at(p)) != std::string_view::npos;
    };
    const size_t cur = cursor_offset();
    size_t p = cur;
    if(!is_br(p)) {
        if(E.mode == INSERT && cur > 0 && is_br(cur - 1)) p = cur - 1;
        else return out;
    }
    const char c = pt.at(p);
    const size_t idx = kPairs.find(c);
    const bool opening = idx % 2 == 0;
    const char other = kPairs[opening ? idx + 1 : idx - 1];

    B.syntax.update(pt, B.eol);
    const auto kind = [&](size_t i) { return B.syntax.literal_at(i).kind; };
    const auto here = kind(p);
    constexpr size_t kWindow = 20000;
    const size_t lo = opening ? p : (p > kWindow ? p - kWindow : 0);
    const size_t hi = opening ? std::min(pt.size(), p + kWindow) : p + 1;
    const std::string win = pt.text(lo, hi - lo);
    int depth = 0;
    for(size_t k = 0; k < win.size(); ++k) {
        const size_t i = opening ? lo + k : hi - 1 - k;
        const char x = win[i - lo];
        if(x != c && x != other) continue;
        if(kind(i) != here) continue;
        if(x == c) ++depth;
        else if(--depth == 0) {
            out = {p, i, true};
            return out;
        }
    }
    return out;
}
}  // namespace

void editor_draw_rows() {
    g_px = tree_cols();
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
                      : vis            ? Range{E.cmd_sel_start, E.cmd_sel_end}
                                       : Range{};
    const bool dg = opts.diagnostics;
    const ParenPair pp = find_paren_pair();
    const bool cl_on = opts.cursorline && !vis;
    const Style cl_st = theme_ui("ui.cursorline", Style{.bg = 0x313244});
    const Style cc_st = theme_ui("ui.colorcolumn", Style{.bg = 0x45475a});
    const Style tr_st = theme_ui("ui.trailing", Style{.bg = 0xf38ba8});
    const Style mp_st =
        theme_ui("ui.matchparen", Style{.bg = 0x585b70, .attrs = ATTR_BOLD});
    std::vector<LineSpan> decos, runs, view_decos;
    std::vector<EolItem> eols;
    SubstView pv;
    std::vector<Span> pv_syn;
    E.buf().sync_anchors();
    md_buffer_refresh(E.buf());
    const bool wrap = opts.wrap;
    int wrap_line = E.row_offset;
    int seg = wrap ? E.row_sub : 0;
    std::vector<int> starts;
    for(int y = 0; y < E.term_rows; ++y) {
        const int file_row = wrap ? wrap_line : y + E.row_offset;
        if(E.buf().pt.size() == 0 && y == 0) {
            const std::string welcome =
                std::format("Winter Editor -- version {}", WINTER_VERSION);
            const int pad =
                std::max(0, (E.term_cols - text_width(welcome)) / 2);
            put_text(
                pad, y, welcome, E.term_cols, theme_ui("ui.welcome", Style{}));
        } else if(file_row >= total) {
            put_text(0, y, "~", E.term_cols, theme_ui("ui.tilde", Style{}));
        } else {
            const size_t nl = view_text.find('\n', pos);
            const size_t len =
                (nl == std::string::npos ? view_text.size() : nl) - pos;
            const std::string_view real(view_text.data() + pos, len);
            const auto& real_spans = E.buf().syntax.line_spans(base + pos, len);
            const bool pvw = subst_preview_line(file_row, real, pv);
            const bool mdv = !pvw && md_line_view(file_row, real, text_cols(), pv);
            const bool xf = pvw || mdv;  // `line` is a transformed view
            const std::string_view line =
                xf ? std::string_view(pv.text) : real;
            const size_t dlen = line.size();  // length of what gets drawn
            if(xf) {
                pv_syn.clear();
                for(Span sp : real_spans) {
                    sp.start = pv.map(sp.start);
                    sp.end = pv.map(sp.end);
                    if(sp.end > sp.start) pv_syn.push_back(sp);
                }
            }
            const std::vector<Span>& spans = xf ? pv_syn : real_spans;
            if(wrap && starts.empty())
                starts = wrap_starts(file_row, text_cols());
            const int nseg = wrap ? static_cast<int>(starts.size()) : 1;
            const int s0 = wrap ? starts[seg] : E.col_offset;
            const int cols =
                wrap && seg + 1 < nseg ? starts[seg + 1] - s0 : text_cols();
            if(g > 0 && seg == 0) {
                const Style gst =
                    file_row == E.cy
                        ? theme_ui("ui.linenr.cursor",
                                   Style{.attrs = ATTR_BOLD})
                        : theme_ui("ui.linenr", Style{.attrs = ATTR_DIM});
                put_text(0, y, gutter_label(file_row, E.cy, g), g, gst);
            }
            const size_t ls = base + pos;
            size_t lo = 0, hi = 0;
            if(vis && sel.end > ls && sel.start <= ls + len) {
                lo = std::max(sel.start, ls) - ls;
                hi = std::min(sel.end, ls + len) - ls;
                if(xf) {
                    lo = pv.map(lo);
                    hi = pv.map(hi);
                }
                if(len == 0)
                    put_text(
                        g,
                        y,
                        " ",
                        E.term_cols,
                        theme_ui("ui.visual", Style{.attrs = ATTR_REVERSE}));
            }
            decos.clear();
            if(dg && !xf) diag_spans(E.buf().diags, ls, len, decos);
            if(!xf) search_spans(file_row, line, decos);
            if(cl_on && file_row == E.cy) decos.push_back({0, dlen, 1, cl_st});
            if(opts.hl_trailing && !(E.mode == INSERT && file_row == E.cy)) {
                // npos + 1 wraps to 0, so an all-blank line is fully trailing
                const size_t keep = line.find_last_not_of(" \t") + 1;
                if(keep < dlen) decos.push_back({keep, dlen, 2, tr_st});
            }
            if(pp.ok && !xf)
                for(const size_t o : {pp.a, pp.b})
                    if(o >= ls && o < ls + len)
                        decos.push_back({o - ls, o - ls + 1, 20, mp_st});
            if(!xf) {
                snippet_spans(ls, len, decos);
                E.buf().decos.spans_on_line(ls, len, decos);
            } else if(mdv) {
                // decorations speak source offsets: carry them over to the
                // concealed text, then lay the icons on top
                view_decos.clear();
                if(dg) diag_spans(E.buf().diags, ls, len, view_decos);
                search_spans(file_row, real, view_decos);
                if(pp.ok)
                    for(const size_t o : {pp.a, pp.b})
                        if(o >= ls && o < ls + len)
                            view_decos.push_back({o - ls, o - ls + 1, 20, mp_st});
                snippet_spans(ls, len, view_decos);
                E.buf().decos.spans_on_line(ls, len, view_decos);
                for(const LineSpan& v : view_decos)
                    decos.push_back({pv.map(v.lo), pv.map(v.hi), v.prio, v.st});
                decos.insert(decos.end(), pv.marks.begin(), pv.marks.end());
            } else {
                decos.insert(decos.end(), pv.marks.begin(), pv.marks.end());
                if(hi > lo) {
                    const Style vst =
                        theme_ui("ui.visual", Style{.attrs = ATTR_REVERSE});
                    size_t cur = lo;
                    for(const LineSpan& m : pv.marks) {
                        if(m.hi <= cur || m.lo >= hi) continue;
                        if(m.lo > cur) decos.push_back({cur, m.lo, 0, vst});
                        cur = std::max(cur, m.hi);
                    }
                    if(cur < hi) decos.push_back({cur, hi, 0, vst});
                    lo = hi = 0;
                }
            }
            compose_spans(decos, runs);
            put_line(y, line, s0, cols, g, spans, lo, hi, runs);
            if(cl_on && file_row == E.cy)  // tint the empty tail of the row too
                for(int x = g_px + g; x < g_px + g + cols && x < scr.width();
                    ++x)
                    if(Cell& c = scr.at(x, y); c == Cell{}) c.style = cl_st;
            if(!pvw)  // whole-line bands (markdown headings, code blocks)
                if(const auto f = E.buf().decos.fill_on_line(ls, len))
                    for(int x = g_px + g; x < g_px + g + cols && x < scr.width();
                        ++x)
                        if(Cell& c = scr.at(x, y); c == Cell{}) c.style = *f;
            if(opts.colorcolumn > 0) {
                const int x = g_px + g + (opts.colorcolumn - 1) - s0;
                if(x >= g_px + g && x < g_px + g + cols && x < scr.width())
                    style_patch(scr.at(x, y).style, cc_st);
            }
            if(seg + 1 == nseg) {  // virtual text goes after the last segment
                eols.clear();
                VirtText diag_vt;
                if(dg && opts.diag_virtual_text)
                    if(const Diag* w =
                           diag_worst_on_line(E.buf().diags, ls, len)) {
                        Style vs = theme_ui("diag.virtual",
                                            Style{.attrs = ATTR_ITALIC});
                        if(vs.fg == 0xFFFFFFFF) vs.fg = diag_color(w->severity);
                        diag_vt = {std::format("■ {}", diag_first_line(*w)),
                                   vs};
                        eols.push_back({kPrioDiag - w->severity, &diag_vt});
                    }
                E.buf().decos.eol_on_line(ls, len, eols);
                put_eols(y, line, g, eols, s0);
            }
            if(++seg >= nseg) {
                seg = 0;
                ++wrap_line;
                starts.clear();
                pos += len + 1;
            }
        }
    }
    g_px = 0;
}

namespace {
struct ScreenPos {
    int x, y;
};

ScreenPos cursor_screen() {
    const int base = tree_cols() + gutter();
    if(!opts.wrap) return {base + g_crx - E.col_offset, E.cy - E.row_offset};
    const int W = text_cols();
    const auto st = wrap_starts(E.cy, W);
    const int cs = seg_of(st, g_crx);
    return {base + g_crx - st[cs],
            rows_between({E.row_offset, E.row_sub}, {E.cy, cs}, W)};
}

}  // namespace

void editor_draw_tree() {
    const int w = tree_cols() - 1;
    if(w <= 0) return;
    const auto& lines = tree_view(E.term_rows);
    for(int y = 0; y < E.term_rows; ++y) {
        const TreeLine& l = lines[static_cast<size_t>(y)];
        const Style sel =
            theme_ui("ui.tree.select", Style{.attrs = ATTR_REVERSE});
        const Style vsel = theme_ui(
            "ui.tree.visual", theme_ui("ui.visual", Style{.attrs = ATTR_REVERSE}));
        const Style fill = l.selected ? sel : l.visual ? vsel : Style{};
        for(int x = 0; x < w; ++x) scr.at(x, y) = Cell{U' ', 1, fill};
        int x = 0;
        for(const TreeSeg& s : l.segs) {
            Style st = s.st;
            if(l.selected) style_patch(st, sel);
            else if(l.visual) style_patch(st, vsel);
            x = put_text(x, y, s.text, w, st);
        }
        scr.at(w, y) =
            Cell{U'│', 1, theme_ui("ui.tree.sep", Style{.attrs = ATTR_DIM})};
    }
}

namespace {
constexpr uint32_t kUnset = 0xFFFFFFFF;

// Draws (or just measures) one side of the bar. Left blocks are followed by
// an arrow into the next block (or the bar), right blocks are preceded by one.
int draw_side(int y, int x, const std::vector<StatusItem>& items,
              uint32_t bar_bg, std::string_view glyph, bool left, bool draw) {
    const int x0 = x;
    uint32_t prev = left ? kUnset : bar_bg;
    const auto boundary = [&](uint32_t from, uint32_t to) {
        if(glyph.empty() || from == kUnset || to == kUnset || from == to)
            return;
        if(draw)
            scr.at(x, y) = Cell{utf8_decode(glyph, 0).cp,
                                1,
                                left ? Style{.fg = from, .bg = to}
                                     : Style{.fg = to, .bg = from}};
        ++x;
    };
    const auto emit = [&](std::string_view text, const Style& st) {
        if(draw) x = put_text(x, y, text, scr.width(), st);
        else x += text_width(text);
    };
    for(const StatusItem& it : items) {
        const StatusBlock& b = it.block;
        boundary(prev, b.base.bg);
        if(b.pad) emit(" ", b.base);
        for(const StatusPiece& p : b.pieces) {
            Style st = b.base;
            style_patch(st, p.st);
            emit(p.text, st);
        }
        if(b.pad) emit(" ", b.base);
        prev = b.base.bg;
    }
    if(left) boundary(prev, bar_bg);
    return x - x0;
}

// Drops the lowest-priority block of either side. False when nothing may go.
bool drop_one(std::vector<StatusItem>& l, std::vector<StatusItem>& r) {
    std::vector<StatusItem>* side = nullptr;
    size_t at = 0;
    int best = 100;  // 100+ (the mode pill) never goes
    for(auto* v : {&l, &r})
        for(size_t i = 0; i < v->size(); ++i)
            if((*v)[i].block.prio < best) {
                best = (*v)[i].block.prio;
                side = v;
                at = i;
            }
    if(!side) return false;
    side->erase(side->begin() + static_cast<std::ptrdiff_t>(at));
    return true;
}
}  // namespace

void editor_draw_status_bar() {
    const int y = E.term_rows;
    const Style bar = statusline_bar_style();
    for(int x = 0; x < scr.width(); ++x) scr.at(x, y) = Cell{U' ', 1, bar};
    const StatusLayout& lay = statusline_layout();
    if(!lay.enabled) return;

    std::vector<StatusItem> L = statusline_eval(lay.left);
    std::vector<StatusItem> R = statusline_eval(lay.right);
    const std::string lg = statusline_sep(true), rg = statusline_sep(false);
    const auto total = [&] {
        return draw_side(y, 0, L, bar.bg, lg, true, false) +
               draw_side(y, 0, R, bar.bg, rg, false, false);
    };
    while(total() + 1 > scr.width() && drop_one(L, R)) {}

    const int lw = draw_side(y, 0, L, bar.bg, lg, true, true);
    const int rw = draw_side(y, 0, R, bar.bg, rg, false, false);
    if(scr.width() - rw >= lw)
        draw_side(y, scr.width() - rw, R, bar.bg, rg, false, true);
}

void editor_draw_message_bar() {
    if(!E.status_message.empty() && time(nullptr) - E.status_message_time < 5) {
        put_text(0,
                 E.term_rows + 1,
                 E.status_message,
                 scr.width(),
                 theme_ui("ui.message", Style{}));
    } else if(opts.diagnostics) {
        const PieceTable& pt = E.buf().pt;
        const size_t ls = pt.line_start(static_cast<size_t>(E.cy));
        if(const Diag* w = diag_worst_on_line(
               E.buf().diags, ls, pt.line_end(static_cast<size_t>(E.cy)) - ls))
            put_text(0,
                     E.term_rows + 1,
                     std::format(
                         "{}: {}", diag_name(w->severity), diag_first_line(*w)),
                     scr.width(),
                     [&] {
                         Style ms = theme_ui("diag.message", Style{});
                         if(ms.fg == 0xFFFFFFFF)
                             ms.fg = diag_color(w->severity);
                         return ms;
                     }());
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

uint64_t overlay_stamp() {
    uint64_t h = reinterpret_cast<uintptr_t>(&E.buf());
    for(const uint64_t v : {E.buf().pt.revision(),
                            static_cast<uint64_t>(E.cx),
                            static_cast<uint64_t>(E.cy),
                            static_cast<uint64_t>(E.mode)})
        h = h * 1000003u ^ v;
    return h;
}

void emit_change_events() {
    static const Buffer* last_buf = nullptr;
    static uint64_t last_rev = 0;
    static int last_cx = -1, last_cy = -1;
    const Buffer* b = &E.buf();
    if(b == last_buf) {
        if(b->pt.revision() != last_rev) events_emit("text_changed");
        if(E.cx != last_cx || E.cy != last_cy) events_emit("cursor_moved");
    }
    last_buf = &E.buf();
    last_rev = E.buf().pt.revision();
    last_cx = E.cx;
    last_cy = E.cy;
}

void editor_refresh_screen() {
    if(resize_pending()) {
        resize_ack();  // clear first: a resize during the query re-arms it
        int rows, cols;
        if(query_window_size(&rows, &cols) == 0) render_resize(rows, cols);
    }
    static uint64_t theme_seen = 0;
    if(const uint64_t r = theme_rev(); r != theme_seen) {
        theme_seen = r;
        for(auto& b : E.buffers) b->syntax.restyle();
        lsp_sync_semantic_refresh();
        scr.invalidate();
    }
    tree_layout(scr.width());
    E.term_cols = scr.width() - tree_cols();
    E.buf().syntax.update(E.buf().pt, E.buf().eol);
    lsp_sync_tick();
    E.buf().sync_anchors();
    emit_change_events();
    editor_scroll();
    overlay_tick();
    scr.clear();
    editor_draw_rows();
    editor_draw_tree();
    editor_draw_status_bar();
    editor_draw_message_bar();
    const PromptView& pv = prompt_view();
    const ScreenPos cp = cursor_screen();
    const Viewport vp{.cols = scr.width(),
                      .text_rows = E.term_rows,
                      .rows = scr.height(),
                      .cur_x = cp.x,
                      .cur_y = cp.y};
    overlay_draw(scr, vp, pv.active);
    int ox = 0, oy = 0;
    const bool ov_cur = !pv.active && overlay_cursor(vp, ox, oy);
    const int sx = pv.active        ? std::min(pv.col, scr.width() - 1)
                   : ov_cur         ? ox
                   : tree_focused() ? 0
                                    : vp.cur_x;
    const int sy = pv.active        ? E.term_rows + 1
                   : ov_cur         ? oy
                   : tree_focused() ? tree_cursor_row()
                                    : vp.cur_y;
    scr.cursor(sx, sy);
    set_cursor_for_mode(pv.active || ov_cur ? INSERT : E.mode);
    // Cursor colour. Never "unset" while cursor_inverse is on: OSC 112 hands
    // the colour to the terminal, whose default can resolve to the background.
    constexpr uint32_t kNone = 0xFFFFFFFF;
    constexpr uint32_t kCursorFallback = 0xffffff;
    constexpr uint32_t kCursorlineBg = 0x313244;
    uint32_t col = theme_ui("ui.cursor", Style{}).fg;
    if(opts.cursor_inverse) {
        if(col == kNone) col = kCursorFallback;
        const bool in_text = !pv.active && !ov_cur && !tree_focused();
        if(in_text && sx >= 0 && sx < scr.width() && sy >= 0 &&
           sy < scr.height()) {
            const Cell& c = scr.at(sx, sy);
            const bool glyph = c.width > 0 && c.chr > U' ' && c.chr != 0x7f;
            // reversed cells draw their fg as the background: skip them
            if(glyph && !(c.style.attrs & ATTR_REVERSE)) {
                const uint32_t cur_bg =
                    theme_ui("ui.search.current", Style{.bg = kSearchCurBg}).bg;
                const uint32_t tok =
                    c.style.bg == cur_bg
                        ? theme_ui("ui.search.cursor",
                                   Style{.fg = kSearchCurCursor})
                              .fg
                        : c.style.fg;
                const Style base = theme_ui("ui.normal", Style{});
                const bool is_bg =
                    tok == c.style.bg || tok == base.bg || tok == kCursorlineBg;
                if(tok != kNone && !is_bg) col = tok;
            }
        }
    }
    set_cursor_color(col);
    scr.flush();
}
