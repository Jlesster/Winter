/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "term_panel.hpp"
#include <stdlib.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "buffer.hpp"
#include "command.hpp"
#include "core.hpp"  // CTRL_KEY
#include "dock.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "output.hpp"
#include "panel.hpp"
#include "pty.hpp"
#include "reg.hpp"
#include "registry.hpp"
#include "term_keys.hpp"
#include "terminal.hpp"  // editor_take_paste
#include "theme.hpp"
#include "utf8.hpp"
#include "vt.hpp"

namespace {

constexpr uint32_t kDefaultColor = 0xFFFFFFFF;
constexpr int kTermPrefix = CTRL_KEY(']');

std::vector<PanelId> g_tabs;       // creation order
PanelId g_cur = 0;                 // the tab :Term works on, 0 = none
OutMode g_mode = OutMode::Bottom;  // where terminals are shown

size_t tab_index(PanelId id) {
    return static_cast<size_t>(std::ranges::find(g_tabs, id) - g_tabs.begin());
}

// "1 [2] 3  " for the panel `self`; empty with a single tab.
std::string tab_strip(PanelId self) {
    if(g_tabs.size() < 2) return {};
    std::string s;
    for(size_t i = 0; i < g_tabs.size(); ++i) {
        const std::string n = std::to_string(i + 1);
        s += g_tabs[i] == self ? "[" + n + "]" : n;
        s += ' ';
    }
    return s + ' ';
}

// Show the current tab where it belongs and hand it the keyboard.
void show_cur() {
    if(!g_cur) return;
    dock_show(g_cur, g_mode);
    dock_focus(true);
}

void tab_new() {
    g_cur = term_open();
    show_cur();
}

void tab_goto(size_t i) {
    if(i >= g_tabs.size()) {
        editor_set_status_message("Term: no tab {}", i + 1);
        return;
    }
    g_cur = g_tabs[i];
    show_cur();
}

void tab_step(int d) {
    const auto n = static_cast<long>(g_tabs.size());
    if(n < 2) {
        editor_set_status_message("Term: only one tab");
        return;
    }
    tab_goto(
        static_cast<size_t>((static_cast<long>(tab_index(g_cur)) + d + n) % n));
}

void toggle_mode() {
    g_mode = g_mode == OutMode::Bottom ? OutMode::Float : OutMode::Bottom;
    show_cur();
}

// Forget a tab and destroy its panel. If it is on show, a neighbour takes
// its place first, so the pane stays open.
void dispose(PanelId id) {
    const size_t i = tab_index(id);
    if(i < g_tabs.size()) g_tabs.erase(g_tabs.begin() + static_cast<long>(i));
    if(g_cur == id)
        g_cur = g_tabs.empty() ? 0 : g_tabs[std::min(i, g_tabs.size() - 1)];
    if(g_cur && dock_current() == id) dock_switch(g_cur);
    dock_remove(id);
}

class TerminalPanel final : public Panel {
public:
    TerminalPanel(std::string cwd, std::vector<std::string> argv)
        : cwd_(std::move(cwd)),
          argv_(std::move(argv)),
          vt_(24, 80) {
        // OSC 10/11 queries (bat, vim, ...) should see the editor's colours.
        const Style n = theme_ui("ui.normal", Style{});
        setenv("WINTER_TERM", "1", 1);  // terminal discovery env var
        if(n.fg != kDefaultColor && n.bg != kDefaultColor)
            vt_.set_default_colors(n.fg, n.bg);
    }

    ~TerminalPanel() override {
        if(pty_) pty_close(pty_);  // on_data / on_exit are dropped by pty_close
        unsetenv("WINTER_TERM");
    }

    void set_id(PanelId id) { id_ = id; }

    // ---- Panel ------------------------------------------------------------

    std::string title() const override {
        std::string t = tab_strip(id_) + "Terminal";
        std::string clean;  // the program sets this: no control bytes
        for(const char c : vt_.title())
            if(static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) clean += c;
        if(!clean.empty()) t += ": " + clean;
        return t;
    }

    std::string status() const override {
        if(copy_) {
            const char* m = vis_ == Vis::Char   ? "VISUAL"
                            : vis_ == Vis::Line ? "V-LINE"
                                                : "COPY";
            return std::format("{} {}/{}:{}  q: live  ",
                               m,
                               cur_v_ + 1,
                               total(),
                               cur_c_ + 1);
        }
        if(!exited_) return {};
        return std::format("exited {}  Enter: restart", code_);
    }

    Style title_style() const override {
        return exited_ ? Style{.fg = 0xf38ba8} : Style{};
    }

    bool raw_keys() const override { return true; }
    bool pinned() const override { return true; }

    // Body row r shows virtual line top + r: the scrollback first, then the
    // live screen. The live view has the screen's first row at the top.
    void paint(const PanelPut& put, int w, int h) override {
        static const VtCell kBlank{};
        const long top = copy_ ? top_ : static_cast<long>(vt_.scrollback_lines());
        const int left = copy_ ? left_ : 0;
        Style sel = theme_ui("ui.visual", Style{.attrs = ATTR_REVERSE});
        std::string run;
        for(int r = 0; r < h; ++r) {
            const long v = top + r;
            if(v >= total() || (!copy_ && r >= vt_.rows())) break;
            // history lines keep the width they had when they scrolled off
            const std::vector<VtCell> l = line(v);
            run.clear();
            Style cur{};
            int x0 = 0;
            bool have = false;
            for(int x = 0; x < w; ++x) {
                const int c = left + x;
                const VtCell& cell =
                    static_cast<size_t>(c) < l.size() ? l[static_cast<size_t>(c)]
                                                      : kBlank;
                if(cell.w == 0) continue;  // right half of a wide character
                Style st = cell.st;
                if(selected(v, c)) {
                    if(sel.bg != kDefaultColor) st.bg = sel.bg;
                    if(sel.fg != kDefaultColor) st.fg = sel.fg;
                    st.attrs |= sel.attrs;
                }
                if(have && !(st == cur)) {
                    put(x0, r, run, cur);
                    run.clear();
                    have = false;
                }
                if(!have) {
                    x0 = x;
                    cur = st;
                    have = true;
                }
                // a wide character cut off by a narrower panel
                const bool cut = cell.w == 2 && x + 1 >= w;
                append_utf8(run, cut || cell.cp == 0 ? U' ' : cell.cp);
            }
            if(have) put(x0, r, run, cur);
        }
    }

    bool cursor(int& x, int& y) const override {
        if(copy_) {
            x = cur_c_ - left_;
            y = static_cast<int>(cur_v_ - top_);
            return x >= 0 && x < w_ && y >= 0 && y < h_;
        }
        const Vt::Cursor c = vt_.cursor();
        if(!c.visible || exited_) return false;
        x = std::min(c.x, w_ - 1);
        y = std::min(c.y, h_ - 1);
        return true;
    }

    void resize(int w, int h) override {
        w_ = std::max(w, 1);
        h_ = std::max(h, 1);
        const Mark before = mark();
        vt_.resize(h_, w_);  // a shorter panel pushes rows into scrollback
        follow_output(before);
        if(pty_) pty_resize(pty_, h_, w_);
        else if(!started_) {  // first draw: the shell is born at the real size
            started_ = true;
            spawn();
        }
    }

    void focus_changed(bool on) override {
        chord_ = false;
        if(vt_.focus_events() && pty_)
            pty_write(pty_, on ? "\x1b[I" : "\x1b[O");
    }

    bool key(int c) override {
        if(c == PASTE) {
            leave_copy();
            paste(editor_take_paste());
            return true;
        }
        if(copy_) return copy_key(c);
        // the alternate screen (less, htop, nvim) has no scrollback: the
        // program owns Shift-PageUp there
        if(!chord_ && !vt_.alt_screen()) {
            if(c == kTermPrefix) return enter_copy(), true;
            if(c == (PAGE_UP | MOD_SHIFT)) {
                enter_copy();
                move_line(-std::max(h_ - 1, 1));
                return true;
            }
        }

        // <C-\> prefix: <C-n> or [ copy mode (also <C-]> and Shift-PageUp),
        // c new tab, n/p next/prev tab, 1-9 tab N, f float/bottom, <C-\> a
        // literal Ctrl-\.
        if(c == CTRL_KEY('\\') && !chord_) {
            chord_ = true;
            return true;
        }
        if(chord_) {
            chord_ = false;
            if((c == CTRL_KEY('n') || c == '[') && !vt_.alt_screen())
                return enter_copy(), true;
            if(c == 'c') return tab_new(), true;
            if(c == 'n') return tab_step(1), true;
            if(c == 'p') return tab_step(-1), true;
            if(c == 'f') return toggle_mode(), true;
            if(c >= '1' && c <= '9')
                return tab_goto(static_cast<size_t>(c - '1')), true;
            send(std::string(1, static_cast<char>(CTRL_KEY('\\'))));
            if(c == CTRL_KEY('\\')) return true;  // a literal Ctrl-\ is sent
        }

        if(exited_) {
            if(c == '\r') restart();
            return true;
        }
        send(term_encode_key(c, {.app_cursor = vt_.app_cursor()}));
        return true;
    }

private:
    // ---- copy mode ------------------------------------------------------------
    // A cursor and visual selection over scrollback + screen, a pseudo
    // buffer: the text is the emulator's grid, nothing is copied into the
    // editor. The child keeps running underneath. Lines are addressed by
    // "virtual line": scrollback first, then the screen. A line that scrolls
    // off keeps its number, so cursor, selection and view stay on their text
    // while output arrives; only the scrollback cap dropping the oldest lines
    // shifts them.

    enum class Vis : uint8_t { None, Char, Line };
    struct Mark {
        uint64_t pushed;
        size_t sb;
    };
    struct Pos {
        long v;
        int c;
    };

    Mark mark() const { return {vt_.sb_pushed(), vt_.scrollback_lines()}; }

    long total() const {
        return static_cast<long>(vt_.scrollback_lines()) + vt_.rows();
    }

    std::vector<VtCell> line(long v) const {
        const long sb = static_cast<long>(vt_.scrollback_lines());
        if(v < sb) return vt_.scrollback_line(static_cast<size_t>(v));
        std::vector<VtCell> r(static_cast<size_t>(vt_.cols()));
        for(int c = 0; c < vt_.cols(); ++c)
            r[static_cast<size_t>(c)] = vt_.cell(static_cast<int>(v - sb), c);
        return r;
    }

    // cells up to the last one that is not a blank
    static int used(const std::vector<VtCell>& l) {
        int n = static_cast<int>(l.size());
        while(n > 0 && (l[static_cast<size_t>(n - 1)].cp == 0 ||
                        l[static_cast<size_t>(n - 1)].cp == U' '))
            --n;
        return n;
    }

    static char32_t cp_of(const std::vector<VtCell>& l, int c) {
        if(c < 0 || static_cast<size_t>(c) >= l.size()) return U' ';
        return l[static_cast<size_t>(c)].cp ? l[static_cast<size_t>(c)].cp : U' ';
    }

    // 0 blank, 1 word, 2 punctuation
    static int cls(char32_t cp) {
        if(cp == U' ' || cp == U'\t' || cp == 0) return 0;
        if(cp >= 0x80 || cp == U'_' || (cp >= U'0' && cp <= U'9') ||
           (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z'))
            return 1;
        return 2;
    }

    // keep the column on a character: inside the text, never on the right
    // half of a wide one
    void fix_col() {
        const std::vector<VtCell> l = line(cur_v_);
        cur_c_ = std::clamp(cur_c_, 0, std::max(used(l) - 1, 0));
        if(cur_c_ > 0 && static_cast<size_t>(cur_c_) < l.size() &&
           l[static_cast<size_t>(cur_c_)].w == 0)
            --cur_c_;
    }

    void keep_in_view() {
        const long last = total() - 1;
        cur_v_ = std::clamp(cur_v_, 0L, last);
        top_ = std::clamp(top_, 0L, std::max(total() - h_, 0L));
        if(cur_v_ < top_) top_ = cur_v_;
        if(cur_v_ >= top_ + h_) top_ = cur_v_ - h_ + 1;
        if(cur_c_ < left_) left_ = cur_c_;
        if(cur_c_ >= left_ + w_) left_ = cur_c_ - w_ + 1;
        left_ = std::max(left_, 0);
    }

    void enter_copy() {
        const Vt::Cursor c = vt_.cursor();
        copy_ = true;
        vis_ = Vis::None;
        pend_ = 0;
        count_ = 0;
        top_ = static_cast<long>(vt_.scrollback_lines());
        left_ = 0;
        cur_v_ = top_ + std::clamp(c.y, 0, vt_.rows() - 1);
        cur_c_ = std::max(c.x, 0);
        fix_col();
        want_c_ = cur_c_;
        keep_in_view();
    }

    void leave_copy() {
        copy_ = false;
        vis_ = Vis::None;
    }

    // Lines were pushed since `before`: the cap may have dropped the oldest
    // ones, which renumbers everything. (scrollback_lines() alone cannot
    // tell, hence sb_pushed().)
    void follow_output(const Mark& before) {
        if(!copy_) return;
        if(vt_.alt_screen()) return leave_copy();
        const long pushed = static_cast<long>(vt_.sb_pushed() - before.pushed);
        const long grew = static_cast<long>(vt_.scrollback_lines()) -
                          static_cast<long>(before.sb);
        const long dropped = std::max(pushed - grew, 0L);
        if(dropped) {
            cur_v_ = std::max(cur_v_ - dropped, 0L);
            anc_v_ = std::max(anc_v_ - dropped, 0L);
            top_ = std::max(top_ - dropped, 0L);
        }
        keep_in_view();
    }

    // ---- motions
    void move_line(long d) {
        cur_v_ = std::clamp(cur_v_ + d, 0L, total() - 1);
        cur_c_ = want_c_;
        fix_col();
    }

    void move_col(int d) {
        const std::vector<VtCell> l = line(cur_v_);
        for(; d > 0; --d) {
            const int step = cur_c_ < static_cast<int>(l.size()) &&
                                     l[static_cast<size_t>(cur_c_)].w == 2
                                 ? 2
                                 : 1;
            if(cur_c_ + step >= used(l)) break;
            cur_c_ += step;
        }
        for(; d < 0 && cur_c_ > 0; ++d) {
            --cur_c_;
            if(cur_c_ > 0 && l[static_cast<size_t>(cur_c_)].w == 0) --cur_c_;
        }
        want_c_ = cur_c_;
    }

    void goto_line(long v) {
        cur_v_ = std::clamp(v, 0L, total() - 1);
        cur_c_ = want_c_;
        fix_col();
    }

    void line_start(bool nonblank) {
        cur_c_ = 0;
        if(nonblank) {
            const std::vector<VtCell> l = line(cur_v_);
            while(cur_c_ < used(l) && cls(cp_of(l, cur_c_)) == 0) ++cur_c_;
        }
        fix_col();
        want_c_ = cur_c_;
    }

    void line_end() {
        cur_c_ = INT_MAX;
        fix_col();
        want_c_ = INT_MAX;  // j / k stay at the end of lines
    }

    // next / previous character; a line break counts as a blank
    bool fwd(Pos& p) const {
        const std::vector<VtCell> l = line(p.v);
        int c = p.c + 1;
        while(c < used(l) && l[static_cast<size_t>(c)].w == 0) ++c;
        if(c < used(l)) {
            p.c = c;
            return true;
        }
        if(p.v + 1 >= total()) return false;
        p = {p.v + 1, 0};
        return true;
    }

    bool bwd(Pos& p) const {
        if(p.c > 0) {
            const std::vector<VtCell> l = line(p.v);
            int c = p.c - 1;
            while(c > 0 && l[static_cast<size_t>(c)].w == 0) --c;
            p.c = c;
            return true;
        }
        if(p.v == 0) return false;
        --p.v;
        p.c = std::max(used(line(p.v)) - 1, 0);
        return true;
    }

    int cls_at(const Pos& p) const { return cls(cp_of(line(p.v), p.c)); }

    void set_pos(const Pos& p) {
        cur_v_ = p.v;
        cur_c_ = p.c;
        fix_col();
        want_c_ = cur_c_;
    }

    void word_next() {  // w
        Pos p{cur_v_, cur_c_};
        const int k = cls_at(p);
        const long v0 = p.v;
        while(p.v == v0 && k != 0 && cls_at(p) == k)
            if(!fwd(p)) return set_pos(p);
        while(cls_at(p) == 0)
            if(!fwd(p)) break;
        set_pos(p);
    }

    void word_end() {  // e
        Pos p{cur_v_, cur_c_};
        if(!fwd(p)) return;
        while(cls_at(p) == 0)
            if(!fwd(p)) break;
        const int k = cls_at(p);
        const long v0 = p.v;
        for(;;) {
            Pos q = p;
            if(!fwd(q) || q.v != v0 || cls_at(q) != k) break;
            p = q;
        }
        set_pos(p);
    }

    void word_prev() {  // b
        Pos p{cur_v_, cur_c_};
        if(!bwd(p)) return;
        while(cls_at(p) == 0)
            if(!bwd(p)) break;
        const int k = cls_at(p);
        const long v0 = p.v;
        for(;;) {
            Pos q = p;
            if(!bwd(q) || q.v != v0 || cls_at(q) != k) break;
            p = q;
        }
        set_pos(p);
    }

    // ---- selection
    bool selected(long v, int c) const {
        if(!copy_ || vis_ == Vis::None) return false;
        long v1 = anc_v_, v2 = cur_v_;
        int c1 = anc_c_, c2 = cur_c_;
        if(v1 > v2 || (v1 == v2 && c1 > c2)) {
            std::swap(v1, v2);
            std::swap(c1, c2);
        }
        if(v < v1 || v > v2) return false;
        if(vis_ == Vis::Line) return true;
        if(v == v1 && c < c1) return false;
        if(v == v2 && c > c2) return false;
        return true;
    }

    // cells c0..c1 (inclusive) as text, without trailing blanks
    static std::string cells_text(const std::vector<VtCell>& l, int c0, int c1) {
        std::string t;
        const int end = std::min(c1, used(l) - 1);
        for(int c = std::max(c0, 0); c <= end; ++c) {
            if(l[static_cast<size_t>(c)].w == 0) continue;
            append_utf8(t, cp_of(l, c));
        }
        return t;
    }

    void yank_lines(long v1, long v2) {
        std::string t;
        for(long v = v1; v <= v2; ++v) t += cells_text(line(v), 0, INT_MAX) + '\n';
        reg_store(RegKind::Yank, std::move(t), true);
        editor_set_status_message("{} lines yanked", v2 - v1 + 1);
    }

    void yank_selection() {
        long v1 = anc_v_, v2 = cur_v_;
        int c1 = anc_c_, c2 = cur_c_;
        if(v1 > v2 || (v1 == v2 && c1 > c2)) {
            std::swap(v1, v2);
            std::swap(c1, c2);
        }
        if(vis_ == Vis::Line) {
            yank_lines(v1, v2);
        } else {
            std::string t;
            for(long v = v1; v <= v2; ++v) {
                if(v > v1) t += '\n';
                t += cells_text(line(v), v == v1 ? c1 : 0, v == v2 ? c2 : INT_MAX);
            }
            const size_t n = t.size();
            reg_store(RegKind::Yank, std::move(t), false);
            editor_set_status_message("{} bytes yanked", n);
        }
        cur_v_ = v1;  // the cursor goes to the start, as in vim
        cur_c_ = c1;
        vis_ = Vis::None;
        fix_col();
        want_c_ = cur_c_;
    }

    void start_visual(Vis kind) {
        if(vis_ == kind) {
            vis_ = Vis::None;
            return;
        }
        if(vis_ == Vis::None) {
            anc_v_ = cur_v_;
            anc_c_ = cur_c_;
        }
        vis_ = kind;
    }

    // While in copy mode the keyboard belongs to it: a stray key must not
    // reach the shell.
    bool copy_key(int c) {
        const long page = std::max(h_ - 1, 1), half = std::max(h_ / 2, 1);
        const int n = std::max(count_, 1);
        const char pend = pend_;
        pend_ = 0;
        if(pend == 'g' && c == 'g') {
            goto_line(0);
            count_ = 0;
            keep_in_view();
            return true;
        }
        if(pend == 'y') {
            if(c == 'y') yank_lines(cur_v_, std::min(cur_v_ + n - 1, total() - 1));
            count_ = 0;
            return true;
        }
        if((c >= '1' && c <= '9') || (c == '0' && count_ > 0)) {
            count_ = std::min(count_ * 10 + (c - '0'), 100000);
            return true;
        }
        count_ = 0;
        switch(c) {
            case 'h':
            case ARROW_LEFT: move_col(-n); break;
            case 'l':
            case ARROW_RIGHT: move_col(n); break;
            case 'k':
            case ARROW_UP: move_line(-n); break;
            case 'j':
            case ARROW_DOWN: move_line(n); break;
            case '0':
            case HOME: line_start(false); break;
            case '^': line_start(true); break;
            case '$':
            case END: line_end(); break;
            case 'w':
                for(int i = 0; i < n; ++i) word_next();
                break;
            case 'e':
                for(int i = 0; i < n; ++i) word_end();
                break;
            case 'b':
                for(int i = 0; i < n; ++i) word_prev();
                break;
            case 'g': pend_ = 'g'; return true;
            case 'G': goto_line(total() - 1); break;
            case 'H': goto_line(top_); break;
            case 'M': goto_line(top_ + h_ / 2); break;
            case 'L': goto_line(top_ + h_ - 1); break;
            case CTRL_KEY('u'): move_line(-half); break;
            case CTRL_KEY('d'): move_line(half); break;
            case CTRL_KEY('b'):
            case PAGE_UP:
            case PAGE_UP | MOD_SHIFT: move_line(-page); break;
            case CTRL_KEY('f'):
            case PAGE_DOWN:
            case PAGE_DOWN | MOD_SHIFT: move_line(page); break;
            case 'v': start_visual(Vis::Char); break;
            case 'V': start_visual(Vis::Line); break;
            case 'o':
                if(vis_ != Vis::None) {
                    std::swap(cur_v_, anc_v_);
                    std::swap(cur_c_, anc_c_);
                    want_c_ = cur_c_;
                }
                break;
            case 'y':
                if(vis_ != Vis::None) yank_selection();
                else pend_ = 'y';
                break;
            case 'Y': yank_lines(cur_v_, std::min(cur_v_ + n - 1, total() - 1)); break;
            case '\x1b':
                if(vis_ != Vis::None) vis_ = Vis::None;
                else leave_copy();
                return true;
            case 'q':
            case 'i':
            case '\r':
            case kTermPrefix: leave_copy(); return true;
            default: break;  // swallowed
        }
        keep_in_view();
        return true;
    }

    void spawn() {
        PtySpec s;
        s.argv = argv_;
        s.cwd = cwd_;
        s.rows = h_;
        s.cols = w_;
        s.on_data = [this](std::string_view d) {
            const Mark before = mark();
            vt_.feed(d);
            follow_output(before);
            const std::string r = vt_.take_reply();  // DA, DSR, OSC colour ...
            if(!r.empty() && pty_) pty_write(pty_, r);
        };
        s.on_exit = [this](int code) { on_exit(code); };
        pty_ = pty_open(std::move(s));
        exited_ = pty_ == 0;
        code_ = 0;
        if(exited_) {
            code_ = 127;
            editor_set_status_message("Terminal: could not start the shell");
        }
    }

    void restart() {
        leave_copy();
        vt_.feed(
            "\x1b"
            "c");  // RIS: clean grid, modes back to defaults
        spawn();
    }

    void on_exit(int code) {
        pty_ = 0;  // pty.cpp has already forgotten it
        exited_ = true;
        code_ = code;
        // A clean exit closes the panel; a failure stays on screen. Deferred:
        // dock_remove() destroys this object.
        if(code == 0 && id_) loop_post([id = id_] { dispose(id); });
    }

    void send(std::string_view s) {
        if(s.empty() || !pty_) return;
        if(!pty_write(pty_, s))
            editor_set_status_message(
                "Terminal: input dropped (child not reading)");
    }

    void paste(std::string t) {
        // A paste must not be able to end its own bracket.
        for(size_t at; (at = t.find("\x1b[201~")) != std::string::npos;)
            t.erase(at, 6);
        if(vt_.bracketed_paste()) {
            t = "\x1b[200~" + t + "\x1b[201~";
        } else {  // a terminal types Enter, not a newline
            std::string o;
            o.reserve(t.size());
            for(size_t i = 0; i < t.size(); ++i) {
                if(t[i] == '\r' && i + 1 < t.size() && t[i + 1] == '\n')
                    continue;
                o += t[i] == '\n' ? '\r' : t[i];
            }
            t = std::move(o);
        }
        send(t);
    }

    std::string cwd_;
    std::vector<std::string> argv_;
    Vt vt_;
    PtyId pty_ = 0;
    PanelId id_ = 0;
    int w_ = 80, h_ = 24;
    bool started_ = false;
    bool exited_ = false;
    int code_ = 0;
    bool chord_ = false;  // Ctrl-\ typed, waiting for the next key
    bool copy_ = false;   // cursor + selection over the scrollback
    Vis vis_ = Vis::None;
    long cur_v_ = 0, anc_v_ = 0, top_ = 0;  // cursor, selection start, view
    int cur_c_ = 0, anc_c_ = 0, want_c_ = 0, left_ = 0;
    int count_ = 0;  // a typed count, 0 = none
    char pend_ = 0;  // g or y waiting for its second key
};

void cmd_term(const CmdLine& a) {
    const std::string sub = a.argv.empty() ? "toggle" : a.argv[0];
    if(sub == "close")
        return dock_close();  // hides it, the shells keep running
    if(sub == "kill") {
        if(g_cur) dispose(g_cur);
        else editor_set_status_message("No terminal");
        return;
    }
    if(sub == "new") return tab_new();
    if(sub == "next") return tab_step(1);
    if(sub == "prev") return tab_step(-1);
    if(!sub.empty() &&
       std::ranges::all_of(sub, [](char c) { return c >= '0' && c <= '9'; })) {
        const unsigned long n = std::stoul(sub);
        return tab_goto(n ? n - 1 : g_tabs.size());  // 0 is no tab
    }
    if(sub == "float" || sub == "bottom") {
        g_mode = sub == "float" ? OutMode::Float : OutMode::Bottom;
    } else if(sub == "toggle") {
        if(g_cur && dock_visible() && dock_current() == g_cur)
            return dock_close();
    } else if(sub != "focus") {
        editor_set_status_message("Term: unknown subcommand '{}'", sub);
        return;
    }
    if(!g_cur) g_cur = term_open();
    show_cur();
}

}  // namespace

PanelId term_open(std::string cwd, std::vector<std::string> argv) {
    auto p = std::make_unique<TerminalPanel>(std::move(cwd), std::move(argv));
    TerminalPanel* raw = p.get();
    const PanelId id = dock_add(std::move(p));
    raw->set_id(id);
    g_tabs.push_back(id);
    return id;
}

void term_register_commands() {
    for(const char* sub : {"toggle",
                           "focus",
                           "new",
                           "next",
                           "prev",
                           "float",
                           "bottom",
                           "close",
                           "kill"}) {
        registry().add_action(std::string("term.") + sub,
                              [sub](Ctx&) { term_run(sub); });
    }
    command_add(
        {.name = "Term",
         .desc = "Open, focus or close the integrated terminal",
         .usage = "[toggle|focus|new|next|prev|N|float|bottom|close|kill]",
         .nargs = Nargs::Opt,
         .complete =
             [](std::string_view lead) {
                 std::vector<CmdCand> out;
                 for(const char* s : {"toggle",
                                      "focus",
                                      "new",
                                      "next",
                                      "prev",
                                      "float",
                                      "bottom",
                                      "close",
                                      "kill"})
                     if(std::string_view(s).starts_with(lead))
                         out.push_back({.text = s});
                 return out;
             },
         .run = cmd_term});
}

void term_run(std::string_view sub) {
    CmdLine a;
    a.argv = {std::string(sub)};
    cmd_term(a);
}

void term_toggle(std::string_view position) {
    OutMode want = g_mode;
    if(position == "float") want = OutMode::Float;
    else if(position == "bottom") want = OutMode::Bottom;
    const bool shown = g_cur && dock_visible() && dock_current() == g_cur;
    if(shown && want == g_mode) return dock_close();
    g_mode = want;
    if(!g_cur) g_cur = term_open();
    show_cur();
}
