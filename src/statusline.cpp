#include "statusline.hpp"

#include <cctype>
#include <format>
#include <map>

#include "columns.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "glyphs.hpp"
#include "options.hpp"
#include "syntax.hpp"
#include "theme.hpp"
#include "utf8.hpp"
#include "virt.hpp"

namespace {

constexpr uint32_t kCrust = 0x11111b, kMantle = 0x181825, kSurface0 = 0x313244,
                   kText = 0xcdd6f4, kSubtext = 0xa6adc8, kBlue = 0x89b4fa,
                   kGreen = 0xa6e3a1, kMauve = 0xcba6f7, kRed = 0xf38ba8,
                   kPeach = 0xfab387, kYellow = 0xf9e2af, kTeal = 0x94e2d5;

std::string_view mode_text() {
    switch(E.mode) {
        case INSERT: return "INSERT";
        case SEARCH: return "SEARCH";
        case VISUAL: return E.v_line ? "V-LINE" : "VISUAL";
        case COMMAND: return "COMMAND";
        case REPLACE: return "REPLACE";
        case FILES: return "FILES";
        case NORMAL:
        case OPPEND: break;
    }
    return "NORMAL";
}

uint32_t mode_accent(std::string_view m) {
    if(m == "INSERT") return kGreen;
    if(m == "VISUAL" || m == "V-LINE") return kMauve;
    if(m == "REPLACE") return kRed;
    if(m == "COMMAND") return kPeach;
    if(m == "SEARCH") return kYellow;
    if(m == "FILES") return kTeal;
    return kBlue;
}

// Keeps the tail of s within w cells, "…" in front when something was cut.
std::string clip_left(std::string_view s, int w) {
    if(text_width(s) <= w) return std::string(s);
    if(w < 2) return "";
    size_t i = 0;
    while(i < s.size() && text_width(s.substr(i)) > w - 1)
        i += utf8_decode(s, i).len;
    return "\u2026" + std::string(s.substr(i));
}

Style chip(std::string_view group) {
    return theme_ui(group, Style{.fg = kText, .bg = kSurface0});
}

Style on_bar(std::string_view group) {
    return theme_ui(group, statusline_bar_style());
}

// ---- built-in components ---------------------------------------------------

StatusBlock c_mode() {
    const std::string_view mode = mode_text();
    // ui.statusline.<normal|insert|visual|vline|replace|command|search|files>
    std::string group = "ui.statusline.";
    for(const char c : mode)
        if(c != '-')
            group +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    StatusBlock b;
    b.pad = false;
    b.prio = 100;
    b.base = theme_ui(
        group,
        Style{.fg = kCrust, .bg = mode_accent(mode), .attrs = ATTR_BOLD});
    b.pieces.push_back({std::format(" {} ", mode), {}});
    return b;
}

StatusBlock c_file() {
    const Buffer& buf = E.buf();
    const std::string& fname = buf.filename;
    StatusBlock b;
    b.prio = 90;
    b.base = chip("ui.statusline.file");
    if(opts.nerdicons && !fname.empty()) {
        const size_t slash = fname.rfind('/');
        const std::string base =
            fname.substr(slash == std::string::npos ? 0 : slash + 1);
        Style ic{};
        if(const auto c = file_icon_color(base)) ic.fg = *c;
        b.pieces.push_back({std::string(file_icon(base)), ic});
    }
    b.pieces.push_back({fname.empty()
                            ? std::string("[No Name]")
                            : clip_left(fname, std::max(12, E.term_cols / 3)),
                        {}});
    if(buf.pt.modified())
        b.pieces.push_back(
            {" \u25cf",
             theme_ui("ui.statusline.modified", Style{.fg = kPeach})});
    return b;
}

StatusBlock c_lines() {
    StatusBlock b;
    b.prio = 20;
    b.base = on_bar("ui.statusline.lines");
    b.pieces.push_back({std::format("{} lines", E.num_rows()), {}});
    return b;
}

StatusBlock c_buffers() {
    StatusBlock b;
    if(E.buffers.size() <= 1) return b;
    b.prio = 30;
    b.base = on_bar("ui.statusline.buffers");
    b.pieces.push_back(
        {std::format("[{}/{}]", E.cur + 1, E.buffers.size()), {}});
    return b;
}

StatusBlock c_diagnostics() {
    StatusBlock b;
    if(!opts.diagnostics) return b;
    b.pad = false;
    b.prio = 60;
    b.base = on_bar("ui.statusline.diagnostics");
    const std::string ds = diag_summary(E.buf().diags);  // "E1 W2 "
    size_t i = 0;
    while(i < ds.size()) {
        const size_t e = ds.find(' ', i);
        const std::string tok =
            ds.substr(i, e == std::string::npos ? e : e - i);
        if(!tok.empty()) {
            const int sev = tok[0] == 'E'   ? 1
                            : tok[0] == 'W' ? 2
                            : tok[0] == 'I' ? 3
                                            : 4;
            Style st{};
            st.fg = diag_color(sev);
            b.pieces.push_back({tok + " ", st});
        }
        if(e == std::string::npos) break;
        i = e + 1;
    }
    return b;
}

StatusBlock c_filetype() {
    StatusBlock b;
    const std::string_view lang = E.buf().syntax.lang();
    if(lang.empty()) return b;
    b.prio = 40;
    b.base = on_bar("ui.statusline.filetype");
    b.pieces.push_back({std::string(lang), {}});
    return b;
}

StatusBlock c_indent() {
    StatusBlock b;
    b.prio = 10;
    b.base = on_bar("ui.statusline.indent");
    b.pieces.push_back(
        {std::format("{}:{}", opt_et() ? "sp" : "tab", opt_sw()),
         {}});
    return b;
}

StatusBlock c_position() {
    StatusBlock b;
    b.prio = 80;
    b.base = theme_ui("ui.statusline.position", chip("ui.statusline.pos"));
    b.pieces.push_back({std::format("{}:{}", E.cy + 1, E.rx + 1), {}});
    return b;
}

StatusBlock c_percent() {
    const int rows = static_cast<int>(E.num_rows());
    StatusBlock b;
    b.prio = 70;
    b.base = theme_ui(
        "ui.statusline.percent",
        Style{
            .fg = kCrust, .bg = mode_accent(mode_text()), .attrs = ATTR_BOLD});
    b.pieces.push_back(
        {E.cy == 0 ? std::string("Top")
         : E.cy + 1 >= rows
             ? std::string("Bot")
             : std::format("{}%", (E.cy * 100) / std::max(1, rows - 1)),
         {}});
    return b;
}

StatusBlock c_gap() {
    StatusBlock b;
    b.pad = false;
    b.prio = 5;
    b.base = statusline_bar_style();
    b.pieces.push_back({" ", {}});
    return b;
}

using CompMap = std::map<std::string, StatusFn, std::less<>>;

CompMap builtin_comps() {
    return {
        {"mode", c_mode},
        {"file", c_file},
        {"lines", c_lines},
        {"buffers", c_buffers},
        {"diagnostics", c_diagnostics},
        {"filetype", c_filetype},
        {"indent", c_indent},
        {"position", c_position},
        {"percent", c_percent},
        {"gap", c_gap},
    };
}

CompMap& comps() {
    static CompMap m = builtin_comps();
    return m;
}

StatusLayout default_layout() {
    StatusLayout l;
    l.left = {"mode", "file", "lines", "buffers"};
    l.right = {"diagnostics", "filetype", "position", "percent"};
    return l;
}

StatusLayout& layout_ref() {
    static StatusLayout l = default_layout();
    return l;
}

}  // namespace

void statusline_add(std::string name, StatusFn fn) {
    comps().insert_or_assign(std::move(name), std::move(fn));
}

bool statusline_remove(std::string_view name) {
    const auto it = comps().find(name);
    if(it == comps().end()) return false;
    comps().erase(it);
    return true;
}

bool statusline_has(std::string_view name) {
    return comps().find(name) != comps().end();
}

std::vector<std::string> statusline_names() {
    std::vector<std::string> out;
    for(const auto& [k, v] : comps()) out.push_back(k);
    return out;
}

StatusLayout& statusline_layout() { return layout_ref(); }
void statusline_reset_layout() { layout_ref() = default_layout(); }

void statusline_reset() {
    comps() = builtin_comps();
    statusline_reset_layout();
}

std::vector<StatusItem> statusline_eval(const std::vector<std::string>& names) {
    std::vector<StatusItem> out;
    for(const std::string& n : names) {
        const auto it = comps().find(n);
        if(it == comps().end()) continue;
        StatusFn fn = it->second;  // the component may redefine itself
        StatusBlock b = fn();
        if(b.pieces.empty()) continue;
        out.push_back({n, std::move(b)});
    }
    return out;
}

Style statusline_bar_style() {
    return theme_ui("ui.statusline", Style{.fg = kSubtext, .bg = kMantle});
}

std::string statusline_sep(bool left) {
    const StatusLayout& l = layout_ref();
    const auto& v = left ? l.sep_left : l.sep_right;
    if(v) return *v;
    if(!opts.nerdicons) return "";
    return left ? "\ue0b0" : "\ue0b2";
}
