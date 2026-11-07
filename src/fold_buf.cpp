#include "fold_buf.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "columns.hpp"
#include "editor.hpp"
#include "fold.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "parser.hpp"
#include "registry.hpp"
#include "utf8.hpp"

namespace {
bool g_hold = false;

std::vector<fold::Raw> ts_folds(Buffer& b) {
    std::vector<fold::Raw> out;
    const std::string_view lang = b.syntax.lang();
    if(lang.empty()) return out;
    b.syntax.fold_ranges(out, parser_query(lang, "folds"));
    return out;
}

std::vector<fold::Raw> indent_provider(Buffer& b) {
    const std::string all = b.pt.text(0, b.pt.size());
    const int ts = opt_ts();
    std::vector<int> ind;
    ind.reserve(b.pt.line_count());
    size_t i = 0;
    while(i <= all.size()) {
        size_t nl = all.find('\n', i);
        if(nl == std::string::npos) nl = all.size();
        int w = 0;
        size_t k = i;
        for(; k < nl && (all[k] == ' ' || all[k] == '\t'); ++k)
            w = all[k] == '\t' ? (w / ts + 1) * ts : w + 1;
        ind.push_back(k == nl ? -1 : w);  // whitespace-only: blank
        i = nl + 1;
    }
    return fold::indent_folds(static_cast<int>(ind.size()), [&](int l) {
        return ind[static_cast<size_t>(l)];
    });
}

}  // namespace

// Lines made only of closers: `}` `);` `});` `]` ...
bool closers_only(std::string_view s) {
    bool any = false;
    for(const char c : s) {
        if(c == ' ' || c == '\t') continue;
        if(c == '}' || c == ')' || c == ']' || c == ';' || c == ',') any = true;
        else return false;
    }
    return any;
}

FoldConfig& fold_config() {
    static FoldConfig c;
    return c;
}

void fold_hold_reveal(bool hold) { g_hold = hold; }

void fold_ensure(Buffer& b) {
    if(!b.folds.stale) return;
    b.syntax.update(b.pt, b.eol);
    const FoldConfig& cfg = fold_config();
    std::vector<fold::Provider> ps;
    for(const std::string& n : cfg.providers) {
        if(n == "treesitter") ps.push_back([&b] { return ts_folds(b); });
        else if(n == "indent")
            ps.push_back([&b] { return indent_provider(b); });
    }
    b.folds.set_ranges(fold::run_providers(ps), cfg.minlines, cfg.levelstart);
    b.folds.clamp_lines(static_cast<int>(b.pt.line_count()));
}

void fold_snap_cursor() {
    Buffer& b = E.buf();
    if(!b.folds.any_closed()) return;
    if(E.cy >= static_cast<int>(b.pt.line_count()))
        E.cy = static_cast<int>(b.pt.line_count()) - 1;
    if(!b.folds.hidden(E.cy)) return;
    E.cy = b.folds.head_of(E.cy);
    const std::string line = b.pt.line_text(static_cast<size_t>(E.cy));
    const int size = static_cast<int>(line.size());
    int cx = std::min(E.cx, size);
    if(E.mode == NORMAL && cx >= size && size > 0)
        cx = static_cast<int>(utf8_prev(line, size));
    E.cx = snap_cx(line, cx);
}

bool fold_reveal(unsigned why) {
    if(g_hold || !(fold_config().open & why)) return false;
    Buffer& b = E.buf();
    if(!b.folds.any_closed()) return false;
    return b.folds.reveal(E.cy);
}

// ---- chords ---------------------------------------------------------------
namespace {

void go_to(int line) {
    Buffer& b = E.buf();
    line = std::clamp(line, 0, static_cast<int>(b.pt.line_count()) - 1);
    apply_motion(motion::to_first_nonblank(
        b.pt, b.pt.line_start(static_cast<size_t>(line)), 0));
}

// run `fn` on fresh ranges; the cursor goes back to the head afterwards
void with_folds(const std::function<bool(fold::State&, int)>& fn) {
    Buffer& b = E.buf();
    fold_ensure(b);
    if(b.folds.ranges().empty()) {
        editor_set_status_message("E490: No fold found");
        return;
    }
    if(!fn(b.folds, E.cy)) return;
    fold_snap_cursor();
}

void bindz(const char* name, std::function<void(Ctx&)> fn, const char* keys) {
    registry().add_action(name, std::move(fn));
    registry().map(NORMAL, parse_keys(keys), name);
}

void nav(std::optional<int> (fold::State::*f)(int) const) {
    Buffer& b = E.buf();
    fold_ensure(b);
    if(const auto l = (b.folds.*f)(E.cy)) go_to(*l);
}
}  // namespace

void register_fold_actions() {
    using S = fold::State;
    motion::set_fold_probe([](const PieceTable& t) -> const fold::State* {
        return E.buffers.empty() || &E.buf().pt != &t ? nullptr
                                                      : &E.buf().folds;
    });
    const auto simple = [](bool (S::*f)(int)) {
        return
            [f](Ctx&) { with_folds([f](S& s, int cy) { return (s.*f)(cy); }); };
    };
    bindz("fold.open", simple(&S::open), "zo");
    bindz("fold.close", simple(&S::close), "zc");
    bindz("fold.toggle", simple(&S::toggle), "za");
    bindz("fold.open_rec", simple(&S::open_rec), "zO");
    bindz("fold.close_rec", simple(&S::close_rec), "zC");
    bindz("fold.toggle_rec", simple(&S::toggle_rec), "zA");
    bindz("fold.reveal", simple(&S::reveal), "zv");
    bindz(
        "fold.open_all",
        [](Ctx&) { with_folds([](S& s, int) { return s.open_all(); }); },
        "zR");
    bindz(
        "fold.close_all",
        [](Ctx&) { with_folds([](S& s, int) { return s.close_all(); }); },
        "zM");
    bindz(
        "fold.more",
        [](Ctx& c) {
            with_folds([&](S& s, int) { return s.more(std::max(c.count, 1)); });
        },
        "zm");
    bindz(
        "fold.fewer",
        [](Ctx& c) {
            with_folds(
                [&](S& s, int) { return s.fewer(std::max(c.count, 1)); });
        },
        "zr");
    bindz(
        "fold.disable",
        [](Ctx&) {
            with_folds([](S& s, int) { return s.set_enabled(false); });
        },
        "zn");
    bindz(
        "fold.enable",
        [](Ctx&) { with_folds([](S& s, int) { return s.set_enabled(true); }); },
        "zN");
    bindz(
        "fold.toggle_enabled",
        [](Ctx&) {
            with_folds([](S& s, int) { return s.set_enabled(!s.enabled()); });
        },
        "zi");
    bindz("fold.next", [](Ctx&) { nav(&S::next_fold_start); }, "zj");
    bindz("fold.prev_end", [](Ctx&) { nav(&S::prev_fold_end); }, "zk");
    bindz("fold.enclosing_start", [](Ctx&) { nav(&S::enclosing_start); }, "[z");
    bindz("fold.enclosing_end", [](Ctx&) { nav(&S::enclosing_end); }, "]z");
}
