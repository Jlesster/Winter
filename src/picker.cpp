#include "picker.hpp"

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>

#include "buflist.hpp"
#include "command.hpp"
#include "core.hpp"
#include "jumplist.hpp"
#include "editor.hpp"
#include "fuzzy.hpp"
#include "glyphs.hpp"
#include "inputbox.hpp"
#include "job.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "menu.hpp"
#include "options.hpp"
#include "overlay.hpp"
#include "piece.hpp"
#include "preview.hpp"
#include "theme.hpp"
#include "tree.hpp"

namespace {

struct Picker {
    PickerSpec spec;
    OverlayId input = 0;
    Menu menu;
    std::vector<PickItem> items;
    std::string query, last_query;
    std::vector<uint32_t> cand;
    bool cand_all = true;
    size_t seen = 0;
    TimerId timer = 0;
    TimerId qtimer = 0;
    OverlayId prev = 0;
    std::unique_ptr<FilePreview> pv;
    std::string want_path;
    int want_line = 0, want_col = 0;
    int prev_w = 0, prev_h = 0;
    TimerId pv_timer = 0;
    bool open = false;
    unsigned serial = 0;
} P;

MenuItem make_item(const PickItem& it, size_t id, std::vector<size_t> hl = {}) {
    MenuItem m{.label = it.text, .detail = it.detail, .hl = std::move(hl), .id = id};
    if(!P.spec.paths) return m;
    const size_t slash = it.text.rfind('/');
    m.dim_to = slash == std::string::npos ? 0 : slash + 1;
    if(opts.nerdicons) {
        const std::string name = it.text.substr(m.dim_to);
        m.icon = std::string(file_icon(name));
        if(const auto c = file_icon_color(name)) m.icon_fg = *c;
    }
    return m;
}

// Hands the rows to the menu and puts "shown/total" in its bottom border.
void show(std::vector<MenuItem> out, size_t matched) {
    const size_t n = P.items.size();
    if(n == 0) P.menu.set_footer("");
    else if(P.spec.fuzzy) P.menu.set_footer(std::format("{}/{}", matched, n));
    else P.menu.set_footer(std::format("{}", n));
    P.menu.set_items(std::move(out));
}

void refilter() {
    P.timer = 0;
    const std::string& q = P.query;
    const size_t n = P.items.size();
    const size_t cap = static_cast<size_t>(std::max(1, P.spec.max_results));
    std::vector<MenuItem> out;

    if(!P.spec.fuzzy || q.empty()) {
        for(size_t i = 0; i < std::min(n, cap); ++i)
            out.push_back(make_item(P.items[i], i));
        P.cand.clear();
        P.cand_all = true;
        P.last_query = q;
        P.seen = n;
        show(std::move(out), n);
        return;
    }

    struct Hit {
        int score;
        uint32_t idx;
    };
    std::vector<Hit> hits;
    fuzzy::Match m;
    const auto test = [&](uint32_t i) {
        if(fuzzy::match(P.items[i].text, q, m)) hits.push_back({m.score, i});
    };
    if(!P.cand_all && q.starts_with(P.last_query)) {
        for(const uint32_t i : P.cand) test(i);
        for(size_t i = P.seen; i < n; ++i) test(static_cast<uint32_t>(i));
    } else {
        for(size_t i = 0; i < n; ++i) test(static_cast<uint32_t>(i));
    }
    P.cand.clear();
    for(const Hit& h : hits) P.cand.push_back(h.idx);
    P.cand_all = false;
    P.last_query = q;
    P.seen = n;

    const size_t top = std::min(hits.size(), cap);
    std::partial_sort(hits.begin(),
                      hits.begin() + static_cast<std::ptrdiff_t>(top),
                      hits.end(),
                      [](const Hit& a, const Hit& b) {
                          if(a.score != b.score) return a.score > b.score;
                          const size_t la = P.items[a.idx].text.size();
                          const size_t lb = P.items[b.idx].text.size();
                          return la != lb ? la < lb : a.idx < b.idx;
                      });
    for(size_t k = 0; k < top; ++k) {
        const PickItem& it = P.items[hits[k].idx];
        fuzzy::match(it.text, q, m);
        out.push_back(make_item(it, hits[k].idx, m.pos));
    }
    show(std::move(out), hits.size());
}

void schedule_refilter(int ms) {
    if(P.timer) return;
    P.timer = loop_after(ms, [] {
        if(P.open) refilter();
        else P.timer = 0;
    });
}

void cancel_timers() {
    if(P.timer) loop_cancel(P.timer);
    if(P.qtimer) loop_cancel(P.qtimer);
    if(P.pv_timer) loop_cancel(P.pv_timer);
    P.timer = P.qtimer = 0;
}

void teardown() {
    if(!P.open) return;
    P.open = false;
    cancel_timers();
    P.menu.close();
    if(P.prev) overlay_close(std::exchange(P.prev, 0));
    P.pv.reset();
    P.want_path.clear();
    auto done = std::move(P.spec.on_close);
    P.items.clear();
    P.items.shrink_to_fit();
    P.cand.clear();
    P.input = 0;
    if(done) done();
}

void on_change(std::string_view t) {
    P.query = std::string(t);
    if(P.spec.fuzzy) {
        if(P.timer) loop_cancel(P.timer);
        P.timer = 0;
        refilter();
        return;
    }
    if(P.qtimer) loop_cancel(P.qtimer);
    P.qtimer = loop_after(120, [] {
        P.qtimer = 0;
        if(P.open && P.spec.on_query) P.spec.on_query(P.query);
    });
}

void on_submit(std::string) {
    std::optional<PickItem> pick;
    if(const MenuItem* sel = P.menu.selected(); sel && sel->id < P.items.size())
        pick = P.items[sel->id];
    const auto accept = P.spec.on_accept;
    teardown();
    if(pick && accept) accept(*pick);
}

void on_cancel() { teardown(); }

void preview_paint(OverlayId id) {
    if(!P.open || !P.pv) return;
    const MenuItem* sel = P.menu.selected();
    if(sel && sel->id < P.items.size()) {
        const PickItem& it = P.items[sel->id];
        P.want_path = it.path.empty() ? it.text : it.path;
        P.want_line = it.line;
        P.want_col = it.col;
    } else {
        P.want_path.clear();
    }
    if(P.want_path.empty()) {
        if(!P.pv->path().empty() && !P.pv_timer)
            P.pv_timer = loop_after(150, [] {
                P.pv_timer = 0;
                if(P.open && P.pv && P.want_path.empty()) P.pv->clear();
            });
    } else if(P.want_path != P.pv->path() && !P.pv_timer) {
        P.pv_timer = loop_after(25, [] {
            P.pv_timer = 0;
            if(P.open && P.pv && !P.want_path.empty() &&
               P.want_path != P.pv->path())
                P.pv->load(P.want_path);
        });
    }
    overlay_set_title(
        id, P.want_path.empty()
                ? std::string()
                : std::filesystem::path(P.want_path).filename().string());
    P.pv->paint(id, P.prev_w, P.prev_h, P.want_path, P.want_line, P.want_col);
}

bool on_key(int c) {
    switch(c) {
    case ARROW_DOWN:
    case CTRL_KEY('n'):
    case CTRL_KEY('j'): P.menu.move(+1); return true;
    case ARROW_UP:
    case CTRL_KEY('p'):
    case CTRL_KEY('k'): P.menu.move(-1); return true;
    case PAGE_DOWN:
        for(int i = 0; i < 8; ++i) P.menu.move(+1);
        return true;
    case PAGE_UP:
        for(int i = 0; i < 8; ++i) P.menu.move(-1);
        return true;
    default: return false;
    }
}

void open_at(const std::string& path, int line, int col) {
    jump_push();
    buffer_open(path);
    if(line <= 0) return;
    std::error_code ec;
    if(E.buf().filename.empty() ||
       !std::filesystem::equivalent(E.buf().filename, path, ec) || ec)
        return;
    const PieceTable& pt = E.buf().pt;
    const size_t l = std::min(static_cast<size_t>(line - 1), pt.line_count() - 1);
    const size_t ls = pt.line_start(l), le = pt.line_end(l);
    size_t off = ls + std::min(static_cast<size_t>(std::max(col, 1) - 1), le - ls);
    if(off >= le && le > ls) off = le - 1;
    while(off > ls && (static_cast<unsigned char>(pt.text(off, 1)[0]) & 0xC0) == 0x80)
        --off;
    set_cursor_offset(off);
}

bool have(const char* exe) {
    const char* path = std::getenv("PATH");
    if(!path) return false;
    std::string_view rest = path;
    while(!rest.empty()) {
        const size_t c = rest.find(':');
        const std::string dir(rest.substr(0, c));
        if(!dir.empty() && access((dir + "/" + exe).c_str(), X_OK) == 0)
            return true;
        if(c == std::string_view::npos) break;
        rest.remove_prefix(c + 1);
    }
    return false;
}

std::string files_cmd() {
    if(have("rg")) return "rg --files --hidden --glob '!.git'";
    if(have("git") && access(".git", F_OK) == 0)
        return "git ls-files --cached --others --exclude-standard";
    return "find . -type f -not -path '*/.git/*' | sed 's|^\\./||'";
}

JobId g_files_job = 0;

JobId g_grep_job = 0;
unsigned g_grep_gen = 0;

std::string shell_quote(std::string_view s) {
    std::string out = "'";
    for(const char ch : s) {
        if(ch == '\'') out += "'\\''";
        else out += ch;
    }
    out += '\'';
    return out;
}

void grep_stop() {
    ++g_grep_gen;
    if(g_grep_job) job_kill(g_grep_job);
    g_grep_job = 0;
}

std::optional<PickItem> parse_grep_line(std::string_view ln, bool has_col) {
    const size_t nul = ln.find('\0');
    if(nul == std::string_view::npos) return std::nullopt;
    std::string_view path = ln.substr(0, nul);
    std::string_view rest = ln.substr(nul + 1);
    if(path.starts_with("./")) path.remove_prefix(2);

    const auto num = [&rest](int& out) {
        const auto [p, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), out);
        if(ec != std::errc{} || p == rest.data() + rest.size() || *p != ':')
            return false;
        rest.remove_prefix(static_cast<size_t>(p - rest.data()) + 1);
        return true;
    };
    PickItem it;
    if(!num(it.line)) return std::nullopt;
    if(has_col && !num(it.col)) return std::nullopt;

    while(!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);
    it.text = std::string(rest);
    for(char& ch : it.text)
        if(static_cast<unsigned char>(ch) < 0x20) ch = ' ';
    it.path = std::string(path);

    constexpr size_t kMaxDetail = 34;
    it.detail = it.path + ":" + std::to_string(it.line);
    if(it.detail.size() > kMaxDetail) {
        size_t cut = it.detail.size() - (kMaxDetail - 1);
        while(cut < it.detail.size() &&
              (static_cast<unsigned char>(it.detail[cut]) & 0xC0) == 0x80)
            ++cut;
        it.detail = "\u2026" + it.detail.substr(cut);
    }
    return it;
}

std::string grep_cmd(std::string_view q, bool rg) {
    if(rg)
        return "rg --column --line-number --no-heading --color=never "
    "--smart-case --null --hidden --glob '!.git' "
    "--max-columns=300 --max-columns-preview -e " +
        shell_quote(q) + " -- .";
        const bool upper = std::ranges::any_of(
            q, [](char ch) { return ch >= 'A' && ch <= 'Z'; });
        return std::string("grep -rnIZE ") + (upper ? "" : "-i ") +
            "--exclude-dir=.git -e " + shell_quote(q) + " .";
}

void grep_search(std::string_view q) {
    grep_stop();
    if(q.empty()) {
        picker_reset();
        return;
    }
    const unsigned gen = g_grep_gen;
    const unsigned serial = picker_serial();
    const bool rg = have("rg");
    auto fresh = std::make_shared<bool>(true);
    auto carry = std::make_shared<std::string>();
    g_grep_job = job_start(
        {.cmd = grep_cmd(q, rg),
            .keep_out = false,
                .split_err = true,
                .on_data =
                [=](std::string_view chunk) {
                    if(gen != g_grep_gen || serial != picker_serial()) return;
                    carry->append(chunk);
                    std::vector<PickItem> batch;
                    size_t from = 0, nl;
                    while((nl = carry->find('\n', from)) != std::string::npos) {
                        if(auto it = parse_grep_line(
                            std::string_view(*carry).substr(from, nl - from), rg))
                            batch.push_back(std::move(*it));
                        from = nl + 1;
                    }
                    carry->erase(0, from);
                    if(batch.empty()) return;
                    if(*fresh) {
                        *fresh = false;
                        picker_reset();
                    }
                    if(!picker_add(std::move(batch))) grep_stop();
                },
                .on_err = [](std::string_view) {},
                .on_exit =
                [=](JobResult) {
                    if(gen != g_grep_gen || serial != picker_serial()) return;
                    if(*fresh) picker_reset();
                }});
}

}  // namespace

void picker_open(PickerSpec spec) {
    picker_close();
    P.spec = std::move(spec);
    P.open = true;
    ++P.serial;
    P.items.clear();
    P.cand.clear();
    P.cand_all = true;
    P.seen = 0;
    P.query.clear();
    P.last_query.clear();

    const int screen_w = E.term_cols + tree_cols();
    const bool with_pv = P.spec.preview && screen_w >= 110;
    // With a preview the list takes the left ~45%, the preview the rest.
    const int total = with_pv ? std::clamp(screen_w - 8, 100, 200)
        : std::clamp(screen_w - 8, std::min(40, screen_w), 110);
        const int w = with_pv ? std::max(44, total * 9 / 20) : total;
        const int x = std::max(0, (screen_w - total) / 2);
        // 3 input rows + 14 list rows, minus the one border row they share
        const int y = std::max(0, (E.term_rows - 16) / 3);

        if(with_pv) {
            const unsigned serial = P.serial;
            P.pv = std::make_unique<FilePreview>();
            P.prev_w = total - w;
            P.prev_h = 16;
            OverlaySpec o;
            o.at = OverlaySpec::At::Screen;
            o.x = x + w;
            o.y = y;
            o.w = P.prev_w;
            o.h = P.prev_h;
            o.z = 59;
            o.framed = true;
            o.dismiss_on_move = false;
            o.esc_closes = false;
            o.on_paint = preview_paint;
            o.on_close = [serial] {
                if(serial == P.serial) P.prev = 0;  // closed behind our back
            };
            P.prev = overlay_open(std::move(o));
        }

    P.menu.set_items({});  // nothing left over from the last picker
    const Style frame = theme_ui("ui.picker.border", Style{.fg = 0xb4befe});
    P.menu.open({.at = OverlaySpec::At::Screen,
        .x = x,
            .y = y + 2,  // top border hides under the input's divider
            .framed = true,
            .max_rows = 12,
            .min_w = w - 2,
            .max_w = w,
            .fixed = true,
            .border_style = frame});
    // "Files> " -> title "Files"; the input line itself gets a glyph prompt
    std::string title = P.spec.prompt;
    while(!title.empty() &&
          (title.back() == '>' || title.back() == ' ' || title.back() == ':'))
        title.pop_back();
    P.input = input_open({.prompt = opts.nerdicons ? "\uf002 " : "> ",
        .w = w,
            .at = OverlaySpec::At::Screen,
            .x = x,
            .y = y,
            .prompt_style = theme_ui("ui.picker.prompt",
                                     Style{.fg = 0xb4befe, .attrs = ATTR_BOLD}),
            .on_change = on_change,
            .on_submit = on_submit,
            .on_cancel = on_cancel,
            .on_key = on_key,
            .border_style = frame,
            .title = std::move(title),
            .join_below = true});
}

void picker_close() {
    if(!P.open) return;
    const OverlayId id = P.input;
    teardown();
    if(id) overlay_close(id);
}

bool picker_active() { return P.open; }
unsigned picker_serial() { return P.serial; }

bool picker_add(std::vector<PickItem> items) {
    if(!P.open) return false;
    const size_t cap = static_cast<size_t>(std::max(1, P.spec.max_results));
    for(PickItem& it : items) {
        if(!P.spec.fuzzy && P.items.size() >= cap) break;
        P.items.push_back(std::move(it));
    }
    schedule_refilter(30);
    return P.spec.fuzzy || P.items.size() < cap;
}

void picker_reset() {
    P.items.clear();
    P.cand.clear();
    P.cand_all = true;
    P.seen = 0;
    P.last_query.clear();
    schedule_refilter(0);
}

void picker_files() {
    PickerSpec s;
    s.prompt = "Files> ";
    s.fuzzy = true;
    s.preview = true;
    s.paths = true;
    s.max_results = 200;
    s.on_accept = [](const PickItem& it) { open_at(it.text, 0, 0); };
    s.on_close = [] {
        if(g_files_job) job_kill(g_files_job);
        g_files_job = 0;
    };
    picker_open(std::move(s));
    const unsigned serial = picker_serial();
    auto carry = std::make_shared<std::string>();
    g_files_job = job_start(
        {.cmd = files_cmd(),
            .keep_out = false,
                .on_data =
                [carry, serial](std::string_view chunk) {
                    if(serial != picker_serial()) return;
                    carry->append(chunk);
                    std::vector<PickItem> batch;
                    size_t from = 0, nl;
                    while((nl = carry->find('\n', from)) != std::string::npos) {
                        if(nl > from) batch.push_back({.text = carry->substr(from, nl - from)});
                        from = nl + 1;
                    }
                    carry->erase(0, from);
                    if(!batch.empty()) picker_add(std::move(batch));
                },
                .on_exit =
                [carry, serial](JobResult) {
                    if(serial != picker_serial()) return;
                    if(!carry->empty()) picker_add({{.text = *carry}});
                    carry->clear();
                }});
}

void picker_grep() {
    PickerSpec s;
    s.prompt = "Grep> ";
    s.fuzzy = false;
    s.preview = true;
    s.max_results = 500;
    s.on_query = grep_search;
    s.on_accept = [](const PickItem& it) { open_at(it.path, it.line, it.col); };
    s.on_close = grep_stop;
    picker_open(std::move(s));
}

void picker_register_commands() {
    command_add({.name = "Files",
        .desc = "Fuzzy file finder",
            .nargs = Nargs::None,
            .run = [](const CmdLine&) { picker_files(); }});
    command_add({.name = "Grep",
                 .desc = "Live grep (rg)",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { picker_grep(); }});
}
