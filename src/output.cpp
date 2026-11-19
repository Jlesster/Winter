#include "output.hpp"
#include "dock.hpp"
#include "render.hpp"
#include "window_ed.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <vector>

#include "buflist.hpp"
#include "columns.hpp"
#include "command.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "grapheme.hpp"
#include "keys.hpp"
#include "options.hpp"
#include "overlay.hpp"
#include "quickfix.hpp"
#include "theme.hpp"
#include "utf8.hpp"
#include "virt.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kMaxLines = 50000;  // then "[output truncated]"
constexpr size_t kMaxLine = 8192;    // bytes kept per line
constexpr size_t kMaxRuns = 8;       // history

struct Seg {
    uint32_t lo, hi;
    Style st;
};
struct Line {
    std::string text;
    std::vector<Seg> segs;  // styled byte ranges, ascending
    int qf = -1;            // quickfix entry this line is a diagnostic for
    char type = 0;          // 'e' 'w' 'n' when qf >= 0
    bool ctx = false;       // a context line of the diagnostic above
};

struct Out {
    OutId id = 0;
    PanelId panel = 0;  // its pane in the dock host
    OutRun run;
    std::vector<Line> lines;
    bool open = false;  // the last line is still being written
    bool full = false;  // line cap reached
    size_t done = 0;    // lines ended by '\n'
    OutState state = OutState::Running;
    int code = 0;
    Clock::time_point t0 = Clock::now(), t1{};
    size_t errors = 0, warnings = 0;
    std::string progress, summary;

    // normaliser state, kept across chunks
    enum class Ps : uint8_t {
        Text,
        Esc,
        Skip1,
        Csi,
        Osc,
        OscEsc
    } ps = Ps::Text;
    std::string csi;
    Style cur{};
    bool pending_cr = false;

    // view state (the panel paints and moves it)
    size_t top = 0, sel = 0;
    bool follow = true;
};

std::vector<std::unique_ptr<Out>> g_runs;
OutId g_next = 1;
OutId g_cur = 0;

Out* find(OutId id) {
    for(auto& r : g_runs)
        if(r->id == id) return r.get();
    return nullptr;
}

// ---- normaliser -----------------------------------------------------------

uint32_t ansi16(int n) {
    static constexpr uint32_t kPal[16] = {0x45475a,
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
    static uint64_t rev = ~uint64_t{0};
    static uint32_t col[16];
    if(rev != theme_rev()) {
        rev = theme_rev();
        for(int i = 0; i < 16; ++i)
            col[i] =
                theme_ui(std::format("output.ansi{}", i), Style{.fg = kPal[i]})
                    .fg;
    }
    return col[n & 15];
}

uint32_t ansi256(int n) {
    if(n < 16) return ansi16(n);
    if(n >= 232) {
        const uint32_t g = static_cast<uint32_t>(8 + 10 * (n - 232));
        return g << 16 | g << 8 | g;
    }
    static constexpr uint32_t kLv[6] = {0, 95, 135, 175, 215, 255};
    const int v = n - 16;
    return kLv[v / 36] << 16 | kLv[(v / 6) % 6] << 8 | kLv[v % 6];
}

void sgr(Out& o, std::string_view p) {
    std::vector<int> v;
    int n = 0;
    bool any = false;
    for(const char ch : p) {
        if(ch >= '0' && ch <= '9') {
            n = std::min(n * 10 + (ch - '0'), 100000);
            any = true;
        } else if(ch == ';' || ch == ':') {
            v.push_back(any ? n : 0);
            n = 0;
            any = false;
        }
    }
    v.push_back(any ? n : 0);
    Style& s = o.cur;
    for(size_t i = 0; i < v.size(); ++i) {
        const int c = v[i];
        if(c == 0) s = Style{};
        else if(c == 1) s.attrs |= ATTR_BOLD;
        else if(c == 2) s.attrs |= ATTR_DIM;
        else if(c == 3) s.attrs |= ATTR_ITALIC;
        else if(c == 4) s.attrs |= ATTR_UNDERLINE;
        else if(c == 7) s.attrs |= ATTR_REVERSE;
        else if(c == 9) s.attrs |= ATTR_STRIKE;
        else if(c == 22)
            s.attrs &= static_cast<uint16_t>(~(ATTR_BOLD | ATTR_DIM));
        else if(c == 23) s.attrs &= static_cast<uint16_t>(~ATTR_ITALIC);
        else if(c == 24) s.attrs &= static_cast<uint16_t>(~ATTR_UNDERLINE);
        else if(c == 27) s.attrs &= static_cast<uint16_t>(~ATTR_REVERSE);
        else if(c == 29) s.attrs &= static_cast<uint16_t>(~ATTR_STRIKE);
        else if(c >= 30 && c <= 37) s.fg = ansi16(c - 30);
        else if(c >= 90 && c <= 97) s.fg = ansi16(c - 90 + 8);
        else if(c >= 40 && c <= 47) s.bg = ansi16(c - 40);
        else if(c >= 100 && c <= 107) s.bg = ansi16(c - 100 + 8);
        else if(c == 39) s.fg = Style{}.fg;
        else if(c == 49) s.bg = Style{}.bg;
        else if(c == 38 || c == 48) {
            uint32_t col = 0xFFFFFFFF;
            if(i + 2 < v.size() && v[i + 1] == 5) {
                col = ansi256(v[i + 2] & 255);
                i += 2;
            } else if(i + 4 < v.size() && v[i + 1] == 2) {
                col = static_cast<uint32_t>((v[i + 2] & 255) << 16 |
                                            (v[i + 3] & 255) << 8 |
                                            (v[i + 4] & 255));
                i += 4;
            }
            (c == 38 ? s.fg : s.bg) = col;
        }
    }
}

void put_bytes(Out& o, std::string_view s) {
    if(!o.open) {
        o.lines.emplace_back();
        o.open = true;
    }
    Line& l = o.lines.back();
    if(l.text.size() + s.size() > kMaxLine) return;
    const uint32_t lo = static_cast<uint32_t>(l.text.size());
    l.text.append(s);
    if(o.cur == Style{}) return;
    const uint32_t hi = lo + static_cast<uint32_t>(s.size());
    if(!l.segs.empty() && l.segs.back().hi == lo && l.segs.back().st == o.cur)
        l.segs.back().hi = hi;
    else l.segs.push_back({lo, hi, o.cur});
}

void newline(Out& o) {
    if(!o.open) o.lines.emplace_back();
    o.open = false;
    o.done = o.lines.size();
    if(o.lines.size() >= kMaxLines && !o.full) {
        o.full = true;
        Line l;
        l.text = "[output truncated]";
        o.lines.push_back(std::move(l));
        o.done = o.lines.size();
    }
}

void feed(Out& o, std::string_view chunk) {
    for(const char ch : chunk) {
        if(o.full) return;
        const auto c = static_cast<unsigned char>(ch);
        switch(o.ps) {
            case Out::Ps::Esc:
                if(c == '[') {
                    o.ps = Out::Ps::Csi;
                    o.csi.clear();
                } else if(c == ']') o.ps = Out::Ps::Osc;
                else if(c == '(' || c == ')' || c == '*' || c == '+')
                    o.ps = Out::Ps::Skip1;
                else o.ps = Out::Ps::Text;
                continue;
            case Out::Ps::Skip1: o.ps = Out::Ps::Text; continue;
            case Out::Ps::Csi:
                if(c >= 0x40 && c <= 0x7e) {
                    if(c == 'm') sgr(o, o.csi);
                    o.ps = Out::Ps::Text;
                } else if(o.csi.size() < 64) {
                    o.csi.push_back(ch);
                }
                continue;
            case Out::Ps::Osc:
                if(c == 0x07) o.ps = Out::Ps::Text;
                else if(c == 0x1b) o.ps = Out::Ps::OscEsc;
                continue;
            case Out::Ps::OscEsc: o.ps = Out::Ps::Text; continue;
            case Out::Ps::Text: break;
        }
        if(o.pending_cr) {  // a bare \r rewrites the line (progress output)
            o.pending_cr = false;
            if(c != '\n' && o.open) {
                o.lines.back().text.clear();
                o.lines.back().segs.clear();
            }
        }
        if(c == '\n') newline(o);
        else if(c == '\r') o.pending_cr = true;
        else if(c == 0x1b) o.ps = Out::Ps::Esc;
        else if(c == '\t') {
            const int col = o.open ? text_width(o.lines.back().text) : 0;
            put_bytes(o, std::string(static_cast<size_t>(8 - col % 8), ' '));
        } else if(c < 0x20 || c == 0x7f) {
            // other control characters are dropped
        } else put_bytes(o, std::string_view(&ch, 1));
    }
}

// ---- presenters -----------------------------------------------------------

Style line_base(const Line& l) {
    switch(l.type) {
        case 'e': return theme_ui("output.error", Style{.fg = 0xf38ba8});
        case 'w': return theme_ui("output.warning", Style{.fg = 0xf9e2af});
        case 'n': return theme_ui("output.note", Style{.fg = 0x89b4fa});
        default: return Style{};
    }
}

std::string run_status(const Out& o) {
    const auto end = o.state == OutState::Running ? Clock::now() : o.t1;
    const double secs = std::chrono::duration<double>(end - o.t0).count();
    std::string st;
    switch(o.state) {
        case OutState::Running: st = "running"; break;
        case OutState::Killed: st = "killed"; break;
        case OutState::Done:
            st = o.code == 0 ? "ok" : std::format("exit {}", o.code);
            break;
    }
    std::string s = std::format("{} {:.1f}s", st, secs);
    if(o.errors) s += std::format(" {}E", o.errors);
    if(o.warnings) s += std::format(" {}W", o.warnings);
    return s;
}

Style state_style(const Out& o) {
    if(o.state == OutState::Running)
        return theme_ui("output.running", Style{.fg = 0xf9e2af});
    if(o.state == OutState::Killed || o.code != 0)
        return theme_ui("output.fail", Style{.fg = 0xf38ba8});
    return theme_ui("output.ok", Style{.fg = 0xa6e3a1});
}

void fix_view(Out& o, size_t vh) {
    const size_t n = o.lines.size();
    if(n == 0) {
        o.top = o.sel = 0;
        return;
    }
    if(o.follow) o.sel = n - 1;
    o.sel = std::min(o.sel, n - 1);
    vh = std::max<size_t>(vh, 1);
    if(o.sel < o.top) o.top = o.sel;
    else if(o.sel >= o.top + vh) o.top = o.sel - vh + 1;
    if(o.top + vh > n) o.top = n > vh ? n - vh : 0;
}

OutMode configured_mode() {
    return static_cast<OutMode>(std::clamp(opts.output_mode, 0, 2));
}

void move_sel(Out& o, long d) {
    if(o.lines.empty()) return;
    const long last = static_cast<long>(o.lines.size()) - 1;
    const long to = std::clamp(static_cast<long>(o.sel) + d, 0L, last);
    o.sel = static_cast<size_t>(to);
    o.follow = to == last;
}

void jump_diag(Out& o, int dir) {
    for(long i = static_cast<long>(o.sel) + dir;
        i >= 0 && i < static_cast<long>(o.lines.size());
        i += dir)
        if(o.lines[static_cast<size_t>(i)].qf >= 0 &&
           !o.lines[static_cast<size_t>(i)].ctx) {
            o.sel = static_cast<size_t>(i);
            o.follow = false;
            return;
        }
}

// Bookkeeping lines (ninja progress, include chains, make chatter): dimmed so
// the findings stand out.
bool is_noise(const Line& l) {
    if(l.qf >= 0) return false;
    std::string_view t = l.text;
    if(t.starts_with("ninja:") || t.starts_with("make[") ||
       t.starts_with("make:") || t.starts_with("In file included from"))
        return true;
    if(t.starts_with('[')) {  // ninja: "[12/44] Compiling ..."
        const size_t e = t.find("] ");
        return e != std::string_view::npos && e <= 12 &&
               t.substr(0, e).find('/') != std::string_view::npos;
    }
    const size_t f = t.find_first_not_of(' ');
    return f != std::string_view::npos && f > 0 &&
           t.substr(f).starts_with("from ") &&
           (t.ends_with(':') || t.ends_with(','));  // "   from x.cpp:11:"
}

// The `^~~~` marker line under a source excerpt (gcc: `   | ~~^~~`).
bool is_caret(std::string_view t) {
    const size_t bar = t.find('|');
    const std::string_view rest =
        bar == std::string_view::npos ? t : t.substr(bar + 1);
    return rest.find_first_of("^~") != std::string_view::npos &&
           rest.find_first_not_of(" ^~") == std::string_view::npos;
}

constexpr int kGutter = 2;  // sigil column + a space

// One run, as a pane of the dock host. It looks the run up by id on every
// call, so it stays harmless if the run was dropped from the history.
class OutputPanel final : public Panel {
public:
    explicit OutputPanel(OutId id)
        : id_(id) {}

    std::string title() const override {
        const Out* o = find(id_);
        if(!o) return {};
        const std::string& info =
            o->state == OutState::Running ? o->progress : o->summary;
        return o->run.title + (info.empty() ? "" : "  " + info);
    }

    std::string status() const override {
        const Out* o = find(id_);
        if(!o) return {};
        return std::format("{}  {}/{}",
                           run_status(*o),
                           o->lines.empty() ? 0 : o->sel + 1,
                           o->lines.size());
    }

    Style title_style() const override {
        const Out* o = find(id_);
        return o ? state_style(*o) : Style{};
    }

    void paint(const PanelPut& put, int w, int h) override {
        Out* o = find(id_);
        if(!o || w < 1 || h < 1) return;
        view_h_ = h;
        fix_view(*o, static_cast<size_t>(view_h_));

        const Style cur_row = theme_ui(
            "output.cursor", Style{.bg = 0x313244, .attrs = ATTR_BOLD});
        for(int r = 0; r < h; ++r) {
            const size_t idx = o->top + static_cast<size_t>(r);
            if(idx >= o->lines.size()) break;
            const Line& l = o->lines[idx];
            const bool sel = focused_ && idx == o->sel;
            Style base = l.ctx && !is_caret(l.text) ? Style{} : line_base(l);
            if(is_noise(l))
                base = theme_ui("output.dim", Style{.fg = 0x6c7086});
            if(sel) {
                put(0, r, std::string(static_cast<size_t>(w), ' '), cur_row);
                style_patch(base, cur_row);
            }
            if(l.qf >= 0 && !l.ctx) {
                Style sg = line_base(l);
                if(sel) sg.bg = cur_row.bg;
                put(0, r, l.type == 'e' ? "✘" : l.type == 'w' ? "▲" : "●", sg);
            }
            int x = kGutter;
            const auto emit = [&](size_t lo, size_t hi, const Style& st) {
                if(hi <= lo || x >= w) return;
                const std::string_view t = dock_clip(
                    std::string_view(l.text).substr(lo, hi - lo), w - x);
                Style s = base;
                style_patch(s, st);
                if(sel) s.bg = cur_row.bg;
                put(x, r, t, s);
                x += text_width(t);
            };
            size_t at = 0;
            for(const Seg& sg : l.segs) {
                emit(at, sg.lo, Style{});
                emit(sg.lo, sg.hi, sg.st);
                at = sg.hi;
            }
            emit(at, l.text.size(), Style{});
            if(text_width(l.text) > w - kGutter) {  // clipped: say so
                Style m = theme_ui("output.dim", Style{.fg = 0x6c7086});
                if(sel) m.bg = cur_row.bg;
                put(w - 1, r, "…", m);
            }
        }
    }

    bool cursor(int& x, int& y) const override {
        const Out* o = find(id_);
        if(!o) return false;
        x = 0;
        const int r = static_cast<int>(o->sel) - static_cast<int>(o->top);
        y = std::clamp(r, 0, std::max(view_h_ - 1, 0));
        return true;
    }

    void focus_changed(bool on) override {
        focused_ = on;
        gpend_ = wpend_ = false;
    }

    bool key(int c) override {
        Out* o = find(id_);
        if(!o) return false;
        const bool gp = gpend_;
        gpend_ = false;
        if(wpend_) {  // CTRL-W h j k l: leave the dock like any window
            wpend_ = false;
            if(c == 'h' || c == 'j' || c == 'k' || c == 'l') {
                win_navigate(c == 'h'   ? win::Dir::Left
                             : c == 'j' ? win::Dir::Down
                             : c == 'k' ? win::Dir::Up
                                        : win::Dir::Right);
            }
            return true;
        }
        const long page = std::max(view_h_ - 1, 1);
        switch(c) {
            case 'j':
            case ARROW_DOWN: move_sel(*o, 1); break;
            case 'k':
            case ARROW_UP: move_sel(*o, -1); break;
            case CTRL_KEY('d'): move_sel(*o, page / 2); break;
            case CTRL_KEY('u'): move_sel(*o, -page / 2); break;
            case PAGE_DOWN: move_sel(*o, page); break;
            case PAGE_UP: move_sel(*o, -page); break;
            case 'G': move_sel(*o, static_cast<long>(o->lines.size())); break;
            case 'g':
                if(gp) {
                    o->sel = 0;
                    o->follow = false;
                } else gpend_ = true;
                break;
            case 'n': jump_diag(*o, 1); break;
            case 'N': jump_diag(*o, -1); break;
            case 'f':
                o->follow = !o->follow;
                if(o->follow && !o->lines.empty()) o->sel = o->lines.size() - 1;
                break;
            case '+':
                opts.output_height = std::min(opts.output_height + 2, 50);
                break;
            case '-':
                opts.output_height = std::max(opts.output_height - 2, 3);
                break;
            case '\r':
                if(o->sel < o->lines.size() && o->lines[o->sel].qf >= 0) {
                    const size_t qf = static_cast<size_t>(o->lines[o->sel].qf);
                    dock_focus(false);
                    qf_jump(qf);
                }
                break;
            case 'q': output_close(); break;
            case '\x1b':
            case CTRL_KEY('l'): dock_focus(false); break;
            case CTRL_KEY('w'): wpend_ = true; break;
            case ':': return false;  // the prompt works as usual
            default: break;          // nothing leaks into the text buffer
        }
        return true;
    }

private:
    OutId id_;
    bool focused_ = false;
    bool gpend_ = false, wpend_ = false;
    int view_h_ = 8;  // body rows last painted
};

// The run on show changes (a new run began, or :Output prev/next).
void set_cur(OutId id) {
    g_cur = id;
    if(const Out* o = find(id); o && dock_visible() && !dock_visible())
        dock_switch(o->panel);
}

}  // namespace

// ---- producer side --------------------------------------------------------

OutId output_begin(OutRun run) {
    auto o = std::make_unique<Out>();
    o->id = g_next++;
    o->run = std::move(run);
    o->t0 = Clock::now();
    const OutId id = o->id;
    o->panel = dock_add(std::make_unique<OutputPanel>(id));
    g_runs.push_back(std::move(o));
    while(g_runs.size() > kMaxRuns) {
        const auto it = std::ranges::find_if(g_runs, [](const auto& r) {
            return r->state != OutState::Running && r->id != g_cur;
        });
        if(it == g_runs.end() || (*it)->id == id) break;
        dock_remove((*it)->panel);
        g_runs.erase(it);
    }
    set_cur(id);
    return id;
}

void output_append(OutId id, std::string_view chunk) {
    if(Out* o = find(id)) feed(*o, chunk);
}

void output_end(OutId id, OutState st, int code) {
    Out* o = find(id);
    if(!o || o->state != OutState::Running) return;
    if(o->open) {
        o->open = false;
        o->done = o->lines.size();
    }
    o->state = st;
    o->code = code;
    o->t1 = Clock::now();
}

size_t output_lines(OutId id) {
    const Out* o = find(id);
    return o ? o->lines.size() : 0;
}

size_t output_complete(OutId id) {
    const Out* o = find(id);
    return o ? o->done : 0;
}

std::string_view output_line(OutId id, size_t i) {
    const Out* o = find(id);
    return o && i < o->lines.size() ? std::string_view(o->lines[i].text)
                                    : std::string_view{};
}

void output_link(OutId id, size_t i, size_t qf, char type, bool primary) {
    Out* o = find(id);
    if(!o || i >= o->lines.size()) return;
    Line& l = o->lines[i];
    if(primary && l.qf < 0) {
        if(type == 'e') ++o->errors;
        else if(type == 'w') ++o->warnings;
    }
    l.qf = static_cast<int>(qf);
    l.type = type;
    l.ctx = !primary;
}

void output_mark(OutId id, size_t i, char type) {
    Out* o = find(id);
    if(o && i < o->lines.size() && o->lines[i].qf < 0) o->lines[i].type = type;
}

void output_progress(OutId id, std::string_view text) {
    if(Out* o = find(id)) o->progress = std::string(text);
}

void output_summary(OutId id, std::string_view text) {
    if(Out* o = find(id)) o->summary = std::string(text);
}

std::string output_text(OutId id) {
    const Out* o = find(id);
    std::string s;
    if(!o) return s;
    for(const Line& l : o->lines) {
        s += l.text;
        s += '\n';
    }
    return s;
}

// ---- presenters -----------------------------------------------------------

OutId output_current() { return g_cur; }

bool output_visible() { return dock_visible() && !dock_pinned(); }

bool output_focused() { return dock_focused() && !dock_pinned(); }

void output_close() {
    if(!dock_pinned()) dock_close();
}

void output_present(OutId id, OutMode m) {
    Out* o = find(id);
    if(!o) return;
    g_cur = id;
    if(m == OutMode::Full) {
        dock_close();
        buffer_show_text("[" + o->run.name + "]", output_text(id));
    } else {
        dock_show(o->panel, m);
    }
}

void output_auto(OutId id, OutAuto ev) {
    const Out* o = find(id);
    if(!o || opts.output_auto == 0) return;
    if(opts.output_auto == 1 && ev == OutAuto::Start && !o->run.streamed)
        return;
    const OutMode m = configured_mode();
    if(m == OutMode::Full && ev == OutAuto::Start) return;
    if(m != OutMode::Full && dock_pinned()) return;
    if(output_visible() && g_cur == id && m != OutMode::Full) return;
    output_present(id, m);
}

void output_focus(bool on) {
    if(on && !output_visible() && find(g_cur))
        output_present(g_cur, configured_mode());
    dock_focus(on);
}

void output_toggle() {
    if(output_visible()) return output_close();
    if(!find(g_cur)) {
        editor_set_status_message("No output yet");
        return;
    }
    output_present(g_cur, configured_mode());
}

// ---- :Output --------------------------------------------------------------

namespace {

void cmd_output(const CmdLine& a) {
    std::string sub = a.argv.empty() ? "toggle" : a.argv[0];
    Out* o = find(g_cur);
    if(sub == "close") return output_close();
    if(sub == "toggle") return output_toggle();
    if(!o) {
        editor_set_status_message("No output yet");
        return;
    }
    if(sub == "bottom")
        output_present(g_cur, OutMode::Bottom), opts.output_mode = 0;
    else if(sub == "float")
        output_present(g_cur, OutMode::Float), opts.output_mode = 1;
    else if(sub == "full") output_present(g_cur, OutMode::Full);
    else if(sub == "show") output_present(g_cur, configured_mode());
    else if(sub == "focus") output_focus(true);
    else if(sub == "kill") {
        if(o->state == OutState::Running && o->run.kill) o->run.kill();
        else editor_set_status_message("Nothing running");
    } else if(sub == "rerun") {
        if(o->run.rerun) o->run.rerun();
        else editor_set_status_message("Cannot rerun this output");
    } else if(sub == "prev" || sub == "next") {
        const auto it = std::ranges::find(g_runs, g_cur, &Out::id);
        const auto n = static_cast<long>(g_runs.size());
        const long i = (static_cast<long>(it - g_runs.begin()) +
                        (sub == "next" ? 1 : -1) + n) %
                       n;
        set_cur(g_runs[static_cast<size_t>(i)]->id);
        if(!output_visible()) output_present(g_cur, configured_mode());
    } else {
        editor_set_status_message("Output: unknown subcommand '{}'", sub);
    }
}

}  // namespace

void output_register_commands() {
    command_add(
        {.name = "Output",
         .desc = "Show, focus or control the build/run output",
         .usage = "[toggle|bottom|float|full|focus|close|kill|rerun|prev|next]",
         .nargs = Nargs::Opt,
         .complete =
             [](std::string_view lead) {
                 std::vector<CmdCand> out;
                 for(const char* s : {"toggle",
                                      "bottom",
                                      "float",
                                      "full",
                                      "focus",
                                      "close",
                                      "kill",
                                      "rerun",
                                      "prev",
                                      "next"})
                     if(std::string_view(s).starts_with(lead))
                         out.push_back({.text = s});
                 return out;
             },
         .run = cmd_output});
}
