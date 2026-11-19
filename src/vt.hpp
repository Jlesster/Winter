#pragma once
// A small VT100/xterm-class terminal emulator core: byte parser, cell grid,
// scrollback. No I/O and no editor dependencies. Feed it what the PTY
// produced, read the grid back, write take_reply() to the PTY.
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef VT_STANDALONE
#include "stub_buffer.hpp"
#else
#include "buffer.hpp"  // Style, ATTR_*
#endif

struct VtCell {
    char32_t cp = U' ';
    Style st{};
    uint8_t w = 1;  // 1 normal, 2 left half of a wide char, 0 right half
};

// Column width of one code point: 0 (combining), 1 or 2. Swap for winter's
// own width logic so the grid and the editor agree.
int vt_cp_width(char32_t cp);

class Vt {
public:
    enum class Mouse : uint8_t { Off, X10, Press, Drag, Any };
    struct Cursor {
        int x, y;
        bool visible;
        int shape;     // DECSCUSR 0..6
        bool pending;  // last column written, next char wraps
    };

    Vt(int rows, int cols, size_t max_scrollback = 10000);

    void feed(std::string_view bytes);
    void resize(int rows, int cols);

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    const VtCell& cell(int row, int col) const {
        return grid()[static_cast<size_t>(row * cols_ + col)];
    }
    size_t scrollback_lines() const { return sb_.size(); }
    const std::vector<VtCell>& scrollback_line(size_t i) const {
        return sb_[i];
    }
    void clear_scrollback() { sb_.clear(); }
    // Lines ever pushed into scrollback. Unlike scrollback_lines() it keeps
    // counting once the cap starts dropping the oldest, so a view can tell how
    // far the content moved under it.
    uint64_t sb_pushed() const { return sb_pushed_; }

    Cursor cursor() const {
        return {x_, y_, cursor_visible_, cursor_shape_, pending_};
    }
    bool alt_screen() const { return alt_; }
    bool app_cursor() const { return app_cursor_; }
    bool app_keypad() const { return app_keypad_; }
    bool bracketed_paste() const { return bracketed_paste_; }
    bool focus_events() const { return focus_events_; }
    bool sync_update() const { return sync_update_; }
    Mouse mouse_mode() const { return mouse_; }
    bool mouse_sgr() const { return mouse_sgr_; }

    const std::string& title() const { return title_; }
    const std::string& cwd() const { return cwd_; }  // OSC 7 payload, raw
    uint64_t revision() const { return rev_; }

    std::string take_reply() { return std::exchange(reply_, {}); }
    bool take_bell() { return std::exchange(bell_, false); }

    void set_palette(const std::array<uint32_t, 16>& p) { pal_ = p; }
    void set_default_colors(uint32_t fg, uint32_t bg) {
        dfg_ = fg;
        dbg_ = bg;
    }

private:
    enum class St : uint8_t {
        Ground,
        Escape,
        EscInter,
        Csi,
        CsiInter,
        CsiIgnore,
        Osc,
        OscEsc,
        Str,
        StrEsc,
    };
    struct Saved {
        bool valid = false;
        int x = 0, y = 0;
        bool pending = false, origin = false;
        Style pen{};
        int gl = 0;
        std::array<char, 4> g{'B', 'B', 'B', 'B'};
    };

    std::vector<VtCell>& grid() { return alt_ ? alt_g_ : prim_g_; }
    const std::vector<VtCell>& grid() const { return alt_ ? alt_g_ : prim_g_; }
    VtCell& at(int r, int c) {
        return grid()[static_cast<size_t>(r * cols_ + c)];
    }

    // parser
    void step(unsigned char b);
    void ground(unsigned char b);
    void utf8_byte(unsigned char b);
    void c0(unsigned char b);
    void esc_dispatch(unsigned char b);
    void csi_dispatch(unsigned char b);
    void osc_dispatch();
    void csi_byte(unsigned char b);
    void begin_csi();
    void finish_param();
    int arg(size_t i, int def) const {
        return i < np_ && p_[i] > 0 ? p_[i] : def;
    }

    // operations
    void print(char32_t cp);
    void put_cell(int x, int y, const VtCell& c);
    VtCell blank() const;
    void erase_cells(int y, int c0, int c1);  // [c0, c1)
    void erase_display(int mode);
    void erase_line(int mode);
    void index();
    void reverse_index();
    void scroll_up(int n, int top, int bot);
    void scroll_down(int n, int top, int bot);
    void insert_lines(int n);
    void delete_lines(int n);
    void insert_chars(int n);
    void delete_chars(int n);
    void move_to(int row, int col);  // origin-aware, 0-based, clamps
    void tab_forward(int n);
    void tab_back(int n);
    void sgr();
    void set_mode(bool priv, int m, bool on);
    void report_mode(bool priv, int m);
    void enter_alt(bool save_cursor, bool clear);
    void leave_alt(bool restore_cursor);
    void save_cursor();
    void restore_cursor();
    void full_reset();
    void soft_reset();
    void reset_tabs(int from);
    uint32_t color256(int n) const;
    void reply(std::string_view s) { reply_ += s; }

    int rows_, cols_;
    size_t max_sb_;
    std::vector<VtCell> prim_g_, alt_g_;
    std::deque<std::vector<VtCell>> sb_;
    uint64_t sb_pushed_ = 0;
    bool alt_ = false;

    int x_ = 0, y_ = 0;
    bool pending_ = false;
    Style pen_{};
    int top_ = 0, bot_ = 0;
    bool origin_ = false, autowrap_ = true, insert_ = false, lnm_ = false;
    bool cursor_visible_ = true;
    int cursor_shape_ = 0;
    bool app_cursor_ = false, app_keypad_ = false, bracketed_paste_ = false;
    bool focus_events_ = false, sync_update_ = false, mouse_sgr_ = false;
    Mouse mouse_ = Mouse::Off;
    std::array<char, 4> charset_{'B', 'B', 'B', 'B'};
    int gl_ = 0;
    std::vector<bool> tabs_;
    Saved saved_[2];
    char32_t last_cp_ = 0;

    std::array<uint32_t, 16> pal_;
    uint32_t dfg_ = 0xcdd6f4, dbg_ = 0x1e1e2e;

    // parser state
    St st_ = St::Ground;
    int need_ = 0;
    char32_t ucp_ = 0, umin_ = 0;
    static constexpr size_t kMaxParams = 32;
    int p_[kMaxParams] = {};
    bool colon_[kMaxParams] = {};
    size_t np_ = 0;
    bool in_param_ = false, next_colon_ = false;
    char priv_ = 0, inter_ = 0;
    std::string osc_;

    std::string title_, cwd_, reply_;
    bool bell_ = false;
    uint64_t rev_ = 0;
};
