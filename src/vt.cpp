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

#include "vt.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

// ---- width ------------------------------------------------------------------

int vt_cp_width(char32_t c) {
    struct R {
        char32_t lo, hi;
    };
    static constexpr R kZero[] = {{0x0300, 0x036F},
                                  {0x0483, 0x0489},
                                  {0x0591, 0x05BD},
                                  {0x0610, 0x061A},
                                  {0x064B, 0x065F},
                                  {0x0E31, 0x0E31},
                                  {0x0E34, 0x0E3A},
                                  {0x1AB0, 0x1AFF},
                                  {0x1DC0, 0x1DFF},
                                  {0x200B, 0x200F},
                                  {0x2028, 0x202E},
                                  {0x2060, 0x2064},
                                  {0x20D0, 0x20FF},
                                  {0xFE00, 0xFE0F},
                                  {0xFE20, 0xFE2F},
                                  {0xFEFF, 0xFEFF},
                                  {0xE0100, 0xE01EF}};
    static constexpr R kWide[] = {
        {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},
        {0x23E9, 0x23EC},   {0x25FD, 0x25FE},   {0x2614, 0x2615},
        {0x2648, 0x2653},   {0x267F, 0x267F},   {0x2693, 0x2693},
        {0x26A1, 0x26A1},   {0x26AA, 0x26AB},   {0x26BD, 0x26BE},
        {0x26C4, 0x26C5},   {0x26CE, 0x26CE},   {0x26D4, 0x26D4},
        {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
        {0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},
        {0x270A, 0x270B},   {0x2728, 0x2728},   {0x274C, 0x274C},
        {0x274E, 0x274E},   {0x2753, 0x2755},   {0x2757, 0x2757},
        {0x2795, 0x2797},   {0x27B0, 0x27B0},   {0x27BF, 0x27BF},
        {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},
        {0x2E80, 0x303E},   {0x3041, 0x33FF},   {0x3400, 0x4DBF},
        {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},   {0xA960, 0xA97F},
        {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},   {0xFE30, 0xFE6F},
        {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},   {0x1F004, 0x1F004},
        {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A},
        {0x1F200, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C},
        {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3},
        {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E},
        {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D},
        {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A},
        {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F},
        {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2},
        {0x1F6D5, 0x1F6D7}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC},
        {0x1F7E0, 0x1F7EB}, {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945},
        {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FAFF}, {0x20000, 0x2FFFD},
        {0x30000, 0x3FFFD}};
    if(c < 0x300) return 1;
    for(const R& r : kZero)
        if(c >= r.lo && c <= r.hi) return 0;
    for(const R& r : kWide)
        if(c >= r.lo && c <= r.hi) return 2;
    return 1;
}

namespace {

constexpr char32_t kDecGfx[31] = {
    U'◆', U'▒', U'␉', U'␌', U'␍', U'␊', U'°', U'±', U'␤', U'␋', U'┘',
    U'┐', U'┌', U'└', U'┼', U'⎺', U'⎻', U'─', U'⎼', U'⎽', U'├', U'┤',
    U'┴', U'┬', U'│', U'≤', U'≥', U'π', U'≠', U'£', U'·'};  // 0x60 .. 0x7E

}  // namespace

// ---- construction
// -------------------------------------------------------------

Vt::Vt(int rows, int cols, size_t max_scrollback)
    : rows_(std::max(rows, 1)),
      cols_(std::max(cols, 1)),
      max_sb_(max_scrollback) {
    pal_ = {0x45475a,
            0xf38ba8,
            0xa6e3a1,
            0xf9e2af,
            0x89b4fa,
            0xf5c2e7,
            0x94e2d5,
            0xbac2de,
            0x585b70,
            0xf38ba8,
            0xa6e3a1,
            0xf9e2af,
            0x89b4fa,
            0xf5c2e7,
            0x94e2d5,
            0xa6adc8};
    const size_t n = static_cast<size_t>(rows_) * static_cast<size_t>(cols_);
    prim_g_.assign(n, VtCell{});
    alt_g_.assign(n, VtCell{});
    bot_ = rows_ - 1;
    reset_tabs(0);
}

void Vt::reset_tabs(int from) {
    tabs_.resize(static_cast<size_t>(cols_), false);
    for(int i = from; i < cols_; ++i)
        tabs_[static_cast<size_t>(i)] = i % 8 == 0;
}

uint32_t Vt::color256(int n) const {
    if(n < 16) return pal_[static_cast<size_t>(n)];
    if(n >= 232) {
        const uint32_t g = static_cast<uint32_t>(8 + 10 * (n - 232));
        return g << 16 | g << 8 | g;
    }
    static constexpr uint32_t kLv[6] = {0, 95, 135, 175, 215, 255};
    const int v = n - 16;
    return kLv[v / 36] << 16 | kLv[(v / 6) % 6] << 8 | kLv[v % 6];
}

VtCell Vt::blank() const {
    VtCell c;
    c.st.bg = pen_.bg;
    return c;
}

// ---- resize
// ---------------------------------------------------------------------

void Vt::resize(int rows, int cols) {
    rows = std::max(rows, 1);
    cols = std::max(cols, 1);
    if(rows == rows_ && cols == cols_) return;
    const auto remap = [&](std::vector<VtCell>& g, int shift) {
        std::vector<VtCell> n(static_cast<size_t>(rows) *
                              static_cast<size_t>(cols));
        for(int r = 0; r < rows; ++r) {
            const int sr = r + shift;
            if(sr < 0 || sr >= rows_) continue;
            for(int c = 0; c < std::min(cols, cols_); ++c)
                n[static_cast<size_t>(r * cols + c)] =
                    g[static_cast<size_t>(sr * cols_ + c)];
            // a wide char cut in half at the new edge
            VtCell& last =
                n[static_cast<size_t>(r * cols + std::min(cols, cols_) - 1)];
            if(cols < cols_ && last.w == 2) last = VtCell{};
        }
        g = std::move(n);
    };
    int shift = 0;
    if(!alt_ && rows < rows_ && y_ >= rows) {
        shift = y_ - rows + 1;  // keep the cursor row visible
        for(int r = 0; r < shift; ++r) {
            std::vector<VtCell> line(prim_g_.begin() + r * cols_,
                                     prim_g_.begin() + (r + 1) * cols_);
            sb_.push_back(std::move(line));
            ++sb_pushed_;
            if(sb_.size() > max_sb_) sb_.pop_front();
        }
    } else if(alt_ && rows < rows_ && y_ >= rows) {
        shift = y_ - rows + 1;
    }
    // remap() reads the old rows_/cols_, so both grids go before they change
    remap(prim_g_, alt_ ? 0 : shift);
    remap(alt_g_, alt_ ? shift : 0);
    rows_ = rows;
    cols_ = cols;
    y_ -= shift;
    y_ = std::clamp(y_, 0, rows_ - 1);
    x_ = std::clamp(x_, 0, cols_ - 1);
    pending_ = false;
    top_ = 0;
    bot_ = rows_ - 1;
    reset_tabs(0);
    ++rev_;
}

// ---- parser
// -----------------------------------------------------------------------

void Vt::feed(std::string_view bytes) {
    if(bytes.empty()) return;
    for(const char ch : bytes) step(static_cast<unsigned char>(ch));
    ++rev_;
}

void Vt::step(unsigned char b) {
    // CAN and SUB abort whatever is in progress
    if(b == 0x18 || b == 0x1A) {
        st_ = St::Ground;
        need_ = 0;
        return;
    }
    switch(st_) {
        case St::Ground: ground(b); return;

        case St::Escape:
            if(b == 0x1B) return;  // ESC ESC
            if(b < 0x20) return c0(b);
            if(b == '[') return begin_csi();
            if(b == ']') {
                osc_.clear();
                st_ = St::Osc;
                return;
            }
            if(b == 'P' || b == 'X' || b == '^' || b == '_') {
                st_ = St::Str;
                return;
            }
            if(b >= 0x20 && b <= 0x2F) {
                inter_ = static_cast<char>(b);
                st_ = St::EscInter;
                return;
            }
            inter_ = 0;
            esc_dispatch(b);
            st_ = St::Ground;
            return;

        case St::EscInter:
            if(b == 0x1B) {
                st_ = St::Escape;
                return;
            }
            if(b < 0x20) return c0(b);
            if(b >= 0x20 && b <= 0x2F)
                return;  // more intermediates: keep the first
            esc_dispatch(b);
            st_ = St::Ground;
            return;

        case St::Csi:
        case St::CsiInter:
        case St::CsiIgnore: csi_byte(b); return;

        case St::Osc:
            if(b == 0x07) {
                osc_dispatch();
                st_ = St::Ground;
            } else if(b == 0x1B) st_ = St::OscEsc;
            else if(osc_.size() < 8192) osc_.push_back(static_cast<char>(b));
            return;
        case St::OscEsc:
            osc_dispatch();
            if(b == '\\') st_ = St::Ground;
            else {  // ESC started a new sequence
                st_ = St::Escape;
                step(b);
            }
            return;

        case St::Str:
            if(b == 0x1B) st_ = St::StrEsc;
            return;
        case St::StrEsc:
            if(b == '\\') st_ = St::Ground;
            else {
                st_ = St::Escape;
                step(b);
            }
            return;
    }
}

void Vt::ground(unsigned char b) {
    if(need_ > 0) {
        if((b & 0xC0) == 0x80) {
            ucp_ = ucp_ << 6 | (b & 0x3F);
            if(--need_ == 0) {
                const bool bad = ucp_ < umin_ || ucp_ > 0x10FFFF ||
                                 (ucp_ >= 0xD800 && ucp_ <= 0xDFFF);
                print(bad ? U'�' : ucp_);
            }
            return;
        }
        need_ = 0;  // truncated sequence
        print(U'�');
    }
    if(b == 0x1B) {
        st_ = St::Escape;
        return;
    }
    if(b < 0x20) return c0(b);
    if(b < 0x7F) return print(b);
    if(b == 0x7F) return;
    if(b < 0xC2) return print(U'�');
    if(b < 0xE0) {
        need_ = 1;
        ucp_ = b & 0x1F;
        umin_ = 0x80;
    } else if(b < 0xF0) {
        need_ = 2;
        ucp_ = b & 0x0F;
        umin_ = 0x800;
    } else if(b < 0xF5) {
        need_ = 3;
        ucp_ = b & 0x07;
        umin_ = 0x10000;
    } else print(U'�');
}

void Vt::c0(unsigned char b) {
    switch(b) {
        case 0x07: bell_ = true; break;
        case 0x08:
            pending_ = false;
            if(x_ > 0) --x_;
            break;
        case 0x09: tab_forward(1); break;
        case 0x0A:
        case 0x0B:
        case 0x0C:
            index();
            if(lnm_) x_ = 0;
            break;
        case 0x0D:
            x_ = 0;
            pending_ = false;
            break;
        case 0x0E: gl_ = 1; break;
        case 0x0F: gl_ = 0; break;
        default: break;
    }
}

void Vt::begin_csi() {
    np_ = 0;
    in_param_ = false;
    next_colon_ = false;
    priv_ = 0;
    inter_ = 0;
    for(size_t i = 0; i < kMaxParams; ++i) {
        p_[i] = 0;
        colon_[i] = false;
    }
    st_ = St::Csi;
}

void Vt::finish_param() {
    if(np_ < kMaxParams) {
        colon_[np_] = next_colon_;
        ++np_;
    }
    in_param_ = false;
    next_colon_ = false;
}

void Vt::csi_byte(unsigned char b) {
    if(b == 0x1B) {
        st_ = St::Escape;
        return;
    }
    if(b < 0x20) return c0(b);
    if(st_ == St::CsiIgnore) {
        if(b >= 0x40 && b <= 0x7E) st_ = St::Ground;
        return;
    }
    if(st_ == St::Csi) {
        if(b >= '0' && b <= '9') {
            if(np_ < kMaxParams) {
                p_[np_] = std::min(p_[np_] * 10 + (b - '0'), 1000000);
                in_param_ = true;
            }
            return;
        }
        if(b == ';' || b == ':') {
            // an empty parameter still counts
            if(np_ < kMaxParams) {
                if(!in_param_) p_[np_] = 0;
                finish_param();
                next_colon_ = b == ':';
                in_param_ = false;
            }
            // mark that a parameter follows even if it stays empty
            in_param_ = true;
            return;
        }
        if(b >= 0x3C && b <= 0x3F) {
            if(np_ == 0 && !in_param_ && !priv_) priv_ = static_cast<char>(b);
            else st_ = St::CsiIgnore;
            return;
        }
    }
    if(b >= 0x20 && b <= 0x2F) {
        if(in_param_) finish_param();
        inter_ = static_cast<char>(b);
        st_ = St::CsiInter;
        return;
    }
    if(b >= 0x40 && b <= 0x7E) {
        if(in_param_) finish_param();
        st_ = St::Ground;
        csi_dispatch(b);
        return;
    }
    st_ = St::CsiIgnore;
}

// ---- printing
// -------------------------------------------------------------------------

void Vt::put_cell(int x, int y, const VtCell& c) {
    // overwriting half of a wide char blanks the other half
    VtCell& d = at(y, x);
    if(d.w == 0 && x > 0) at(y, x - 1) = blank();
    if(d.w == 2 && x + 1 < cols_) at(y, x + 1) = blank();
    d = c;
}

void Vt::print(char32_t cp) {
    char g = charset_[static_cast<size_t>(gl_)];
    if(g == '0' && cp >= 0x60 && cp <= 0x7E) cp = kDecGfx[cp - 0x60];
    const int w = vt_cp_width(cp);
    if(w == 0) return;  // combining marks are dropped for now
    if(pending_ && autowrap_) {
        x_ = 0;
        index();
        pending_ = false;
    }
    pending_ = false;
    if(w == 2 && cols_ < 2) return;
    if(w == 2 && x_ == cols_ - 1) {
        if(autowrap_) {
            put_cell(x_, y_, blank());
            x_ = 0;
            index();
        } else {
            return;  // nowhere to put it
        }
    }
    if(insert_) insert_chars(w);
    VtCell c;
    c.cp = cp;
    c.st = pen_;
    c.w = static_cast<uint8_t>(w);
    put_cell(x_, y_, c);
    if(w == 2) {
        VtCell r;
        r.cp = U' ';
        r.st = pen_;
        r.w = 0;
        put_cell(x_ + 1, y_, r);
    }
    last_cp_ = cp;
    x_ += w;
    if(x_ >= cols_) {
        x_ = cols_ - 1;
        pending_ = autowrap_;
    }
}

// ---- erase / scroll
// ----------------------------------------------------------------------

void Vt::erase_cells(int y, int c0, int c1) {
    c0 = std::max(c0, 0);
    c1 = std::min(c1, cols_);
    if(c0 >= c1) return;
    if(c0 > 0 && at(y, c0).w == 0) at(y, c0 - 1) = blank();
    if(c1 < cols_ && at(y, c1).w == 0) at(y, c1) = blank();
    const VtCell b = blank();
    for(int c = c0; c < c1; ++c) at(y, c) = b;
}

void Vt::erase_display(int mode) {
    pending_ = false;
    if(mode == 0) {
        erase_cells(y_, x_, cols_);
        for(int r = y_ + 1; r < rows_; ++r) erase_cells(r, 0, cols_);
    } else if(mode == 1) {
        for(int r = 0; r < y_; ++r) erase_cells(r, 0, cols_);
        erase_cells(y_, 0, x_ + 1);
    } else if(mode == 2) {
        for(int r = 0; r < rows_; ++r) erase_cells(r, 0, cols_);
    } else if(mode == 3) {
        sb_.clear();
    }
}

void Vt::erase_line(int mode) {
    pending_ = false;
    if(mode == 0) erase_cells(y_, x_, cols_);
    else if(mode == 1) erase_cells(y_, 0, x_ + 1);
    else if(mode == 2) erase_cells(y_, 0, cols_);
}

void Vt::scroll_up(int n, int top, int bot) {
    n = std::min(n, bot - top + 1);
    if(n <= 0) return;
    if(!alt_ && top == 0 && bot == rows_ - 1) {
        for(int r = 0; r < n; ++r) {
            sb_.emplace_back(prim_g_.begin() + r * cols_,
                             prim_g_.begin() + (r + 1) * cols_);
            ++sb_pushed_;
            if(sb_.size() > max_sb_) sb_.pop_front();
        }
    }
    auto& g = grid();
    std::move(g.begin() + (top + n) * cols_,
              g.begin() + (bot + 1) * cols_,
              g.begin() + top * cols_);
    for(int r = bot - n + 1; r <= bot; ++r) erase_cells(r, 0, cols_);
}

void Vt::scroll_down(int n, int top, int bot) {
    n = std::min(n, bot - top + 1);
    if(n <= 0) return;
    auto& g = grid();
    std::move_backward(g.begin() + top * cols_,
                       g.begin() + (bot + 1 - n) * cols_,
                       g.begin() + (bot + 1) * cols_);
    for(int r = top; r < top + n; ++r) erase_cells(r, 0, cols_);
}

void Vt::index() {
    if(y_ == bot_) scroll_up(1, top_, bot_);
    else if(y_ < rows_ - 1) ++y_;
}

void Vt::reverse_index() {
    if(y_ == top_) scroll_down(1, top_, bot_);
    else if(y_ > 0) --y_;
}

void Vt::insert_lines(int n) {
    if(y_ < top_ || y_ > bot_) return;
    scroll_down(n, y_, bot_);
}

void Vt::delete_lines(int n) {
    if(y_ < top_ || y_ > bot_) return;
    n = std::min(n, bot_ - y_ + 1);
    auto& g = grid();
    std::move(g.begin() + (y_ + n) * cols_,
              g.begin() + (bot_ + 1) * cols_,
              g.begin() + y_ * cols_);
    for(int r = bot_ - n + 1; r <= bot_; ++r) erase_cells(r, 0, cols_);
}

void Vt::insert_chars(int n) {
    pending_ = false;
    n = std::min(n, cols_ - x_);
    if(n <= 0) return;
    auto& g = grid();
    VtCell* row = &g[static_cast<size_t>(y_ * cols_)];
    std::move_backward(row + x_, row + cols_ - n, row + cols_);
    const VtCell b = blank();
    for(int c = x_; c < x_ + n; ++c) row[c] = b;
    if(x_ + n < cols_ && row[x_ + n].w == 0) row[x_ + n] = b;
    if(row[cols_ - 1].w == 2) row[cols_ - 1] = b;
    if(x_ > 0 && row[x_].w == 0) row[x_] = b;
}

void Vt::delete_chars(int n) {
    pending_ = false;
    n = std::min(n, cols_ - x_);
    if(n <= 0) return;
    auto& g = grid();
    VtCell* row = &g[static_cast<size_t>(y_ * cols_)];
    if(x_ > 0 && row[x_].w == 0) row[x_ - 1] = blank();
    std::move(row + x_ + n, row + cols_, row + x_);
    const VtCell b = blank();
    for(int c = cols_ - n; c < cols_; ++c) row[c] = b;
    if(row[x_].w == 0) row[x_] = b;
}

void Vt::move_to(int row, int col) {
    pending_ = false;
    if(origin_) {
        row = std::clamp(row + top_, top_, bot_);
    } else {
        row = std::clamp(row, 0, rows_ - 1);
    }
    y_ = row;
    x_ = std::clamp(col, 0, cols_ - 1);
}

void Vt::tab_forward(int n) {
    pending_ = false;
    while(n-- > 0 && x_ < cols_ - 1) {
        ++x_;
        while(x_ < cols_ - 1 && !tabs_[static_cast<size_t>(x_)]) ++x_;
    }
}

void Vt::tab_back(int n) {
    pending_ = false;
    while(n-- > 0 && x_ > 0) {
        --x_;
        while(x_ > 0 && !tabs_[static_cast<size_t>(x_)]) --x_;
    }
}

// ---- cursor save / alt screen
// -----------------------------------------------------------

void Vt::save_cursor() {
    Saved& s = saved_[alt_ ? 1 : 0];
    s = {true, x_, y_, pending_, origin_, pen_, gl_, charset_};
}

void Vt::restore_cursor() {
    const Saved& s = saved_[alt_ ? 1 : 0];
    if(!s.valid) {
        x_ = y_ = 0;
        pending_ = false;
        origin_ = false;
        pen_ = Style{};
        gl_ = 0;
        charset_ = {'B', 'B', 'B', 'B'};
        return;
    }
    x_ = std::clamp(s.x, 0, cols_ - 1);
    y_ = std::clamp(s.y, 0, rows_ - 1);
    pending_ = s.pending;
    origin_ = s.origin;
    pen_ = s.pen;
    gl_ = s.gl;
    charset_ = s.g;
}

void Vt::enter_alt(bool save, bool clear) {
    if(alt_) return;
    if(save) save_cursor();
    alt_ = true;
    if(clear) {
        const VtCell b = blank();
        std::fill(alt_g_.begin(), alt_g_.end(), b);
    }
}

void Vt::leave_alt(bool restore) {
    if(!alt_) return;
    alt_ = false;
    if(restore) restore_cursor();
}

void Vt::soft_reset() {
    pen_ = Style{};
    origin_ = false;
    autowrap_ = true;
    insert_ = false;
    lnm_ = false;
    cursor_visible_ = true;
    app_cursor_ = false;
    app_keypad_ = false;
    top_ = 0;
    bot_ = rows_ - 1;
    charset_ = {'B', 'B', 'B', 'B'};
    gl_ = 0;
    pending_ = false;
    saved_[alt_ ? 1 : 0] = Saved{};
}

void Vt::full_reset() {
    soft_reset();
    leave_alt(false);
    saved_[0] = saved_[1] = Saved{};
    bracketed_paste_ = false;
    focus_events_ = false;
    sync_update_ = false;
    mouse_ = Mouse::Off;
    mouse_sgr_ = false;
    cursor_shape_ = 0;
    reset_tabs(0);
    for(int r = 0; r < rows_; ++r) erase_cells(r, 0, cols_);
    x_ = y_ = 0;
}

// ---- escape sequences
// ----------------------------------------------------------------------

void Vt::esc_dispatch(unsigned char b) {
    if(inter_ == '(' || inter_ == ')' || inter_ == '*' || inter_ == '+') {
        const size_t slot = inter_ == '('   ? 0
                            : inter_ == ')' ? 1
                            : inter_ == '*' ? 2
                                            : 3;
        charset_[slot] = b == '0' ? '0' : 'B';
        return;
    }
    if(inter_ == '#') {
        if(b == '8') {  // DECALN
            for(int r = 0; r < rows_; ++r)
                for(int c = 0; c < cols_; ++c) {
                    VtCell e;
                    e.cp = U'E';
                    at(r, c) = e;
                }
            x_ = y_ = 0;
            pending_ = false;
        }
        return;
    }
    if(inter_) return;
    switch(b) {
        case '7': save_cursor(); break;
        case '8': restore_cursor(); break;
        case 'D': index(); break;
        case 'E':
            x_ = 0;
            index();
            break;
        case 'M': reverse_index(); break;
        case 'H': tabs_[static_cast<size_t>(x_)] = true; break;
        case 'c': full_reset(); break;
        case '=': app_keypad_ = true; break;
        case '>': app_keypad_ = false; break;
        default: break;
    }
}

void Vt::sgr() {
    if(np_ == 0) {
        pen_ = Style{};
        return;
    }
    Style& s = pen_;
    for(size_t i = 0; i < np_; ++i) {
        const int c = p_[i];
        switch(c) {
            case 0: s = Style{}; break;
            case 1: s.attrs |= ATTR_BOLD; break;
            case 2: s.attrs |= ATTR_DIM; break;
            case 3: s.attrs |= ATTR_ITALIC; break;
            case 4:
                if(i + 1 < np_ && colon_[i + 1]) {
                    const int sub = p_[++i];
                    s.attrs &= static_cast<uint16_t>(
                        ~(ATTR_UNDERLINE | ATTR_UNDERCURL));
                    if(sub == 3) s.attrs |= ATTR_UNDERCURL;
                    else if(sub != 0) s.attrs |= ATTR_UNDERLINE;
                } else s.attrs |= ATTR_UNDERLINE;
                break;
            case 7: s.attrs |= ATTR_REVERSE; break;
            case 9: s.attrs |= ATTR_STRIKE; break;
            case 21: s.attrs |= ATTR_UNDERLINE; break;
            case 22:
                s.attrs &= static_cast<uint16_t>(~(ATTR_BOLD | ATTR_DIM));
                break;
            case 23: s.attrs &= static_cast<uint16_t>(~ATTR_ITALIC); break;
            case 24:
                s.attrs &=
                    static_cast<uint16_t>(~(ATTR_UNDERLINE | ATTR_UNDERCURL));
                break;
            case 27: s.attrs &= static_cast<uint16_t>(~ATTR_REVERSE); break;
            case 29: s.attrs &= static_cast<uint16_t>(~ATTR_STRIKE); break;
            case 39: s.fg = Style{}.fg; break;
            case 49: s.bg = Style{}.bg; break;
            case 59: s.ul = Style{}.ul; break;
            case 38:
            case 48:
            case 58: {
                uint32_t col = 0xFFFFFFFF;
                bool ok = false;
                if(i + 1 < np_ && colon_[i + 1]) {
                    // 38:5:n   38:2:r:g:b   38:2:cs:r:g:b
                    size_t n = 0;
                    while(i + 1 + n < np_ && colon_[i + 1 + n]) ++n;
                    const int kind = p_[i + 1];
                    if(kind == 5 && n >= 2) {
                        col = color256(p_[i + 2] & 255);
                        ok = true;
                    } else if(kind == 2 && n >= 4) {
                        const size_t o =
                            n >= 5 ? i + 3 : i + 2;  // skip colour space
                        col = static_cast<uint32_t>((p_[o] & 255) << 16 |
                                                    (p_[o + 1] & 255) << 8 |
                                                    (p_[o + 2] & 255));
                        ok = true;
                    }
                    i += n;
                } else if(i + 2 < np_ && p_[i + 1] == 5) {
                    col = color256(p_[i + 2] & 255);
                    ok = true;
                    i += 2;
                } else if(i + 4 < np_ && p_[i + 1] == 2) {
                    col = static_cast<uint32_t>((p_[i + 2] & 255) << 16 |
                                                (p_[i + 3] & 255) << 8 |
                                                (p_[i + 4] & 255));
                    ok = true;
                    i += 4;
                }
                if(ok) (c == 38 ? s.fg : c == 48 ? s.bg : s.ul) = col;
                break;
            }
            default:
                if(c >= 30 && c <= 37) s.fg = pal_[static_cast<size_t>(c - 30)];
                else if(c >= 90 && c <= 97)
                    s.fg = pal_[static_cast<size_t>(c - 90 + 8)];
                else if(c >= 40 && c <= 47)
                    s.bg = pal_[static_cast<size_t>(c - 40)];
                else if(c >= 100 && c <= 107)
                    s.bg = pal_[static_cast<size_t>(c - 100 + 8)];
                break;
        }
    }
}

void Vt::set_mode(bool priv, int m, bool on) {
    if(!priv) {
        if(m == 4) insert_ = on;
        else if(m == 20) lnm_ = on;
        return;
    }
    switch(m) {
        case 1: app_cursor_ = on; break;
        case 6:
            origin_ = on;
            move_to(0, 0);
            break;
        case 7: autowrap_ = on; break;
        case 9: mouse_ = on ? Mouse::X10 : Mouse::Off; break;
        case 25: cursor_visible_ = on; break;
        case 47:
        case 1047:
            if(on) enter_alt(false, true);
            else leave_alt(false);
            break;
        case 1049:
            if(on) enter_alt(true, true);
            else leave_alt(true);
            break;
        case 1000: mouse_ = on ? Mouse::Press : Mouse::Off; break;
        case 1002: mouse_ = on ? Mouse::Drag : Mouse::Off; break;
        case 1003: mouse_ = on ? Mouse::Any : Mouse::Off; break;
        case 1004: focus_events_ = on; break;
        case 1006: mouse_sgr_ = on; break;
        case 2004: bracketed_paste_ = on; break;
        case 2026: sync_update_ = on; break;
        default: break;
    }
}

void Vt::report_mode(bool priv, int m) {
    int state = 0;  // 0 unknown, 1 set, 2 reset
    if(priv) {
        switch(m) {
            case 1: state = app_cursor_ ? 1 : 2; break;
            case 6: state = origin_ ? 1 : 2; break;
            case 7: state = autowrap_ ? 1 : 2; break;
            case 25: state = cursor_visible_ ? 1 : 2; break;
            case 47:
            case 1047:
            case 1049: state = alt_ ? 1 : 2; break;
            case 1004: state = focus_events_ ? 1 : 2; break;
            case 1006: state = mouse_sgr_ ? 1 : 2; break;
            case 2004: state = bracketed_paste_ ? 1 : 2; break;
            case 2026: state = sync_update_ ? 1 : 2; break;
            default: break;
        }
    } else {
        if(m == 4) state = insert_ ? 1 : 2;
        else if(m == 20) state = lnm_ ? 1 : 2;
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "\x1b[%s%d;%d$y", priv ? "?" : "", m, state);
    reply(buf);
}

void Vt::csi_dispatch(unsigned char f) {
    const int n = arg(0, 1);
    char buf[64];

    if(priv_ == '?') {
        if(inter_ == '$' && f == 'p') return report_mode(true, p_[0]);
        if(f == 'h' || f == 'l') {
            for(size_t i = 0; i < std::max<size_t>(np_, 1); ++i)
                set_mode(true, p_[i], f == 'h');
        } else if(f == 'n' && np_ >= 1 && p_[0] == 6) {
            std::snprintf(buf,
                          sizeof buf,
                          "\x1b[?%d;%dR",
                          y_ + 1 - (origin_ ? top_ : 0),
                          x_ + 1);
            reply(buf);
        }
        return;
    }
    if(priv_ == '>') {
        if(f == 'c') reply("\x1b[>0;0;0c");
        else if(f == 'q') reply("\x1bP>|winter-vt\x1b\\");
        return;
    }
    if(priv_) return;
    if(inter_ == '$' && f == 'p') return report_mode(false, p_[0]);
    if(inter_ == ' ' && f == 'q') {
        cursor_shape_ = std::clamp(p_[0], 0, 6);
        return;
    }
    if(inter_ == '!' && f == 'p') return soft_reset();
    if(inter_) return;

    switch(f) {
        case '@': insert_chars(n); break;
        case 'A':
            pending_ = false;
            y_ = std::max(y_ - n, y_ >= top_ ? top_ : 0);
            break;
        case 'B':
        case 'e':
            pending_ = false;
            y_ = std::min(y_ + n, y_ <= bot_ ? bot_ : rows_ - 1);
            break;
        case 'C':
        case 'a':
            pending_ = false;
            x_ = std::min(x_ + n, cols_ - 1);
            break;
        case 'D':
            pending_ = false;
            x_ = std::max(x_ - n, 0);
            break;
        case 'E':
            pending_ = false;
            x_ = 0;
            y_ = std::min(y_ + n, y_ <= bot_ ? bot_ : rows_ - 1);
            break;
        case 'F':
            pending_ = false;
            x_ = 0;
            y_ = std::max(y_ - n, y_ >= top_ ? top_ : 0);
            break;
        case 'G':
        case '`':
            pending_ = false;
            x_ = std::clamp(n - 1, 0, cols_ - 1);
            break;
        case 'H':
        case 'f': move_to(arg(0, 1) - 1, arg(1, 1) - 1); break;
        case 'I': tab_forward(n); break;
        case 'J': erase_display(np_ ? p_[0] : 0); break;
        case 'K': erase_line(np_ ? p_[0] : 0); break;
        case 'L': insert_lines(n); break;
        case 'M': delete_lines(n); break;
        case 'P': delete_chars(n); break;
        case 'S': scroll_up(n, top_, bot_); break;
        case 'T': scroll_down(n, top_, bot_); break;
        case 'X': {
            pending_ = false;
            erase_cells(y_, x_, x_ + n);
            break;
        }
        case 'Z': tab_back(n); break;
        case 'b':
            if(last_cp_)
                for(int i = 0; i < std::min(n, 65536); ++i) print(last_cp_);
            break;
        case 'c':
            if(!np_ || p_[0] == 0) reply("\x1b[?62;22c");
            break;
        case 'd': move_to(arg(0, 1) - 1, x_); break;
        case 'g':
            if(!np_ || p_[0] == 0) tabs_[static_cast<size_t>(x_)] = false;
            else if(p_[0] == 3) std::fill(tabs_.begin(), tabs_.end(), false);
            break;
        case 'h':
        case 'l':
            for(size_t i = 0; i < std::max<size_t>(np_, 1); ++i)
                set_mode(false, p_[i], f == 'h');
            break;
        case 'm': sgr(); break;
        case 'n':
            if(np_ && p_[0] == 5) reply("\x1b[0n");
            else if(np_ && p_[0] == 6) {
                std::snprintf(buf,
                              sizeof buf,
                              "\x1b[%d;%dR",
                              y_ + 1 - (origin_ ? top_ : 0),
                              x_ + 1);
                reply(buf);
            }
            break;
        case 'r': {
            if(!np_) {
                top_ = 0;
                bot_ = rows_ - 1;
                move_to(0, 0);
                break;
            }
            const int t = arg(0, 1) - 1;
            const int b = arg(1, rows_) - 1;
            if(t < b && b < rows_) {
                top_ = t;
                bot_ = b;
                move_to(0, 0);
            }
            break;
        }
        case 's': save_cursor(); break;
        case 'u': restore_cursor(); break;
        case 't':
            if(np_ && p_[0] == 18) {
                std::snprintf(buf, sizeof buf, "\x1b[8;%d;%dt", rows_, cols_);
                reply(buf);
            }
            break;
        default: break;
    }
}

void Vt::osc_dispatch() {
    const size_t semi = osc_.find(';');
    const std::string_view cmd = std::string_view(osc_).substr(0, semi);
    const std::string_view arg_ = semi == std::string::npos
                                      ? std::string_view{}
                                      : std::string_view(osc_).substr(semi + 1);
    if(cmd == "0" || cmd == "2") title_ = std::string(arg_);
    else if(cmd == "7") cwd_ = std::string(arg_);
    else if((cmd == "10" || cmd == "11") && arg_ == "?") {
        const uint32_t c = cmd == "10" ? dfg_ : dbg_;
        char buf[64];
        std::snprintf(buf,
                      sizeof buf,
                      "\x1b]%.*s;rgb:%02x%02x/%02x%02x/%02x%02x\x1b\\",
                      static_cast<int>(cmd.size()),
                      cmd.data(),
                      (c >> 16) & 255,
                      (c >> 16) & 255,
                      (c >> 8) & 255,
                      (c >> 8) & 255,
                      c & 255,
                      c & 255);
        reply(buf);
    }
}
