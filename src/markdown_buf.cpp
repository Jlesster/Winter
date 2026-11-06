// Buffer backend: decorates markdown buffers from md_parse().
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "columns.hpp"
#include "command.hpp"
#include "editor.hpp"
#include "glyphs.hpp"
#include "markdown.hpp"
#include "syntax.hpp"
#include "theme.hpp"
#include "virt.hpp"

namespace {

bool g_on = true;
constexpr size_t kMaxBytes = size_t{1} << 20;  // bigger files are left alone

// prio: fills < cursorline (1) < block roles < inline roles < markers
constexpr int kPrioFill = 0, kPrioBlock = 5, kPrioInline = 6, kPrioMarker = 7;

bool inline_role(MdRole r) {
    switch(r) {
        case MdRole::Code:
        case MdRole::Strong:
        case MdRole::Emph:
        case MdRole::Strike:
        case MdRole::Link:
        case MdRole::LinkUrl: return true;
        default: return false;
    }
}

// Quote bars, bullets and checkboxes are coloured, not dimmed.
bool dim_marker(MdRole r) {
    return r != MdRole::Bullet && r != MdRole::Checkbox && r != MdRole::Quote;
}


// ---- display view (conceal + icons) ---------------------------------------
//
// render-markdown.nvim style: away from the cursor line, syntax characters
// disappear and a few of them turn into icons. Each edit replaces a source
// range of one line; md_line_view() splices them into a SubstView, which the
// renderer already knows how to draw, wrap and map decorations through.

constexpr int kPrioView = 8;  // view marks sit over every deco span

constexpr std::string_view kHeadIcons[6] = {
    "\U000f0ca1 ", "\U000f0ca3 ", "\U000f0ca5 ",
    "\U000f0ca7 ", "\U000f0ca9 ", "\U000f0cab "};
constexpr std::string_view kBullets[4] = {"●", "○", "◆", "◇"};

struct CalloutDef {
    std::string_view kind, icon, title;
    uint32_t color;
};
constexpr uint32_t kInfo = 0x89b4fa, kOk = 0xa6e3a1, kWarn = 0xf9e2af,
                   kErr = 0xf38ba8, kSpecial = 0xcba6f7, kGrey = 0x9399b2;
constexpr CalloutDef kCallouts[] = {
    {"note", "\U000f02fd", "Note", kInfo},
    {"tip", "\U000f0336", "Tip", kOk},
    {"important", "\U000f017e", "Important", kSpecial},
    {"warning", "\U000f002a", "Warning", kWarn},
    {"caution", "\U000f0ce6", "Caution", kErr},
    {"abstract", "\U000f0a38", "Abstract", kInfo},
    {"summary", "\U000f0a38", "Summary", kInfo},
    {"tldr", "\U000f0a38", "Tldr", kInfo},
    {"info", "\U000f02fd", "Info", kInfo},
    {"todo", "\U000f05e1", "Todo", kInfo},
    {"hint", "\U000f0336", "Hint", kOk},
    {"success", "\U000f012c", "Success", kOk},
    {"check", "\U000f012c", "Check", kOk},
    {"done", "\U000f012c", "Done", kOk},
    {"question", "\U000f0625", "Question", kWarn},
    {"help", "\U000f0625", "Help", kWarn},
    {"faq", "\U000f0625", "Faq", kWarn},
    {"attention", "\U000f002a", "Attention", kWarn},
    {"failure", "\U000f0156", "Failure", kErr},
    {"fail", "\U000f0156", "Fail", kErr},
    {"missing", "\U000f0156", "Missing", kErr},
    {"danger", "\U000f140c", "Danger", kErr},
    {"error", "\U000f140c", "Error", kErr},
    {"bug", "\U000f0a30", "Bug", kErr},
    {"example", "\U000f0279", "Example", kSpecial},
    {"quote", "\U000f11a8", "Quote", kGrey},
    {"cite", "\U000f11a8", "Cite", kGrey},
};

struct LinkIcon {
    std::string_view host, icon;
};
constexpr LinkIcon kLinkHosts[] = {
    {"github.com", "\U000f02a4 "},        {"gitlab.com", "\U000f0ba0 "},
    {"stackoverflow.com", "\U000f04cc "}, {"reddit.com", "\U000f044d "},
    {"wikipedia.org", "\U000f05ac "},     {"youtube.com", "\U000f05c3 "},
    {"youtu.be", "\U000f05c3 "},
};

std::string link_icon(std::string_view url, bool image) {
    if(!opts.nerdicons) return {};
    if(image) return "\U000f0976 ";
    if(url.starts_with("mailto:")) return "\U000f0013 ";
    for(const LinkIcon& h : kLinkHosts)
        if(url.find(h.host) != std::string_view::npos)
            return std::string(h.icon);
    if(url.starts_with("http")) return "\U000f059f ";
    return "\U000f0339 ";
}

// fence info string -> file extension for the devicon
std::string_view lang_ext(std::string_view lang) {
    static constexpr std::pair<std::string_view, std::string_view> kAlias[] = {
        {"python", "py"},  {"rust", "rs"},        {"javascript", "js"},
        {"typescript", "ts"}, {"bash", "sh"},     {"shell", "sh"},
        {"console", "sh"}, {"yaml", "yml"},       {"markdown", "md"},
        {"c++", "cpp"},    {"golang", "go"},      {"csharp", "cs"},
        {"c#", "cs"},      {"ruby", "rb"},        {"kotlin", "kt"},
        {"text", "txt"},   {"latex", "tex"},      {"jsonc", "json"},
        {"make", "mk"},    {"objc", "m"},
    };
    for(const auto& [k, v] : kAlias)
        if(k == lang) return v;
    return lang;
}

std::string fence_info(std::string_view line) {
    size_t i = line.find_first_not_of('`');
    while(i != std::string_view::npos && i < line.size() &&
          (line[i] == ' ' || line[i] == '{' || line[i] == '.'))
        ++i;
    if(i == std::string_view::npos || i >= line.size()) return {};
    size_t j = i;
    while(j < line.size() && line[j] != ' ' && line[j] != '{' &&
          line[j] != '}' && line[j] != ',')
        ++j;
    return std::string(line.substr(i, j - i));
}

bool is_delim_row(std::string_view s) {
    bool dash = false, pipe = false;
    for(const char c : s) {
        if(c == '-') dash = true;
        else if(c == '|') pipe = true;
        else if(c != ':' && c != ' ' && c != '\t') return false;
    }
    return dash && pipe;
}

struct ViewBuilder {
    std::string_view src;
    const MdDoc& d;
    const bool nf = opts.nerdicons;
    std::vector<std::vector<MdEdit>> per;  // per source line, line-relative
    std::vector<char> skip;                // frontmatter / table: no inline edits
    std::vector<std::pair<size_t, size_t>> heads;  // table header rows

    ViewBuilder(std::string_view s, const MdDoc& doc)
        : src(s), d(doc), per(doc.lines.size()), skip(doc.lines.size(), 0) {}

    size_t line_at(size_t pos) const {
        const auto it =
            std::ranges::upper_bound(d.lines, pos, {}, &MdLine::lo);
        return static_cast<size_t>(it - d.lines.begin()) - (it != d.lines.begin());
    }

    void add(size_t li, size_t lo, size_t hi, std::string text, Style st = {},
             bool fill = false) {
        const MdLine& l = d.lines[li];
        if(hi <= lo || lo < l.lo || hi > l.hi) return;
        st.attrs = static_cast<uint16_t>(st.attrs | ATTR_NODIM);
        per[li].push_back({static_cast<uint32_t>(lo - l.lo),
                           static_cast<uint32_t>(hi - l.lo), std::move(text),
                           st, fill});
    }
    void conceal(size_t lo, size_t hi) {
        if(hi > lo) add(line_at(lo), lo, hi, {});
    }

    static const CalloutDef* find_callout(std::string_view kind) {
        std::string low(kind);
        for(char& c : low)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for(const CalloutDef& c : kCallouts)
            if(c.kind == low) return &c;
        return nullptr;
    }

    Style callout_style(const CalloutDef& c) const {
        return theme_ui(std::format("md.callout.{}", c.kind),
                        Style{.fg = c.color});
    }

    void frontmatter() {
        if(d.lines.size() < 2) return;
        const MdLine& f = d.lines[0];
        if(src.substr(f.tl, f.th - f.tl) != "---") return;
        for(size_t i = 1; i < d.lines.size(); ++i) {
            const MdLine& l = d.lines[i];
            const std::string_view t = src.substr(l.tl, l.th - l.tl);
            if(t == "---" || t == "...") {
                std::fill(skip.begin(), skip.begin() + i + 1, char(2));
                return;
            }
        }
    }

    void blocks() {
        const Style rule_st = theme_ui("md.rule", Style{.fg = 0x585b70});
        const Style quote_st = md_style(MdRole::Quote);
        const Style lang_st =
            theme_ui("md.code.lang", Style{.fg = 0xa6adc8, .attrs = ATTR_BOLD});
        bool in_fence = false;
        std::vector<size_t> indents;  // open list nesting, in columns
        const CalloutDef* callout = nullptr;
        for(size_t i = 0; i < d.lines.size(); ++i) {
            const MdLine& l = d.lines[i];
            if(skip[i] == 2) continue;
            if(l.kind != MdBlock::Quote) callout = nullptr;
            if(l.kind != MdBlock::Bullet && l.kind != MdBlock::Blank &&
               l.kind != MdBlock::CodeLine && l.tl == l.lo)
                indents.clear();
            switch(l.kind) {
                case MdBlock::Blank:
                case MdBlock::CodeLine: break;
                case MdBlock::Text: break;
                case MdBlock::Heading: {
                    if(!nf || l.level < 1 || l.level > 6) break;
                    const Style hs = md_style(MdRole::Heading, l.level);
                    add(i, l.tl, l.cl, std::string(kHeadIcons[l.level - 1]),
                        Style{.fg = hs.fg, .attrs = ATTR_BOLD});
                    break;
                }
                case MdBlock::Rule:
                    add(i, l.tl, l.th, "─", rule_st, true);
                    break;
                case MdBlock::Fence: {
                    const bool open = !in_fence;
                    in_fence = !in_fence;
                    const size_t n = src.find_first_not_of('`', l.tl) == std::string_view::npos
                                         ? l.th - l.tl
                                         : src.find_first_not_of('`', l.tl) - l.tl;
                    const std::string lang =
                        open ? fence_info(src.substr(l.tl, l.th - l.tl)) : "";
                    if(lang.empty() || !nf) {
                        add(i, l.tl, l.th, {});
                        break;
                    }
                    const std::string name = "x." + std::string(lang_ext(lang));
                    Style is{};
                    if(const auto c = file_icon_color(name)) is.fg = *c;
                    add(i, l.tl, l.tl + n, std::string(file_icon(name)), is);
                    add(i, l.tl + n, l.th, lang, lang_st);
                    break;
                }
                case MdBlock::Quote: {
                    const bool first = i == 0 || d.lines[i - 1].kind != MdBlock::Quote;
                    if(first && l.cl + 2 < l.ch && src[l.cl] == '[' &&
                       src[l.cl + 1] == '!') {
                        const size_t close = src.find(']', l.cl);
                        if(close != std::string_view::npos && close < l.ch)
                            callout = find_callout(
                                src.substr(l.cl + 2, close - l.cl - 2));
                        if(callout) {
                            size_t e = close + 1;
                            if(e < l.ch && (src[e] == '+' || src[e] == '-')) ++e;
                            size_t rest = e;
                            while(rest < l.ch && src[rest] == ' ') ++rest;
                            std::string t(nf ? callout->icon : "");
                            if(rest >= l.ch) {  // no custom title: spell one
                                if(!t.empty()) t += ' ';
                                t += callout->title;
                            }
                            Style cs = callout_style(*callout);
                            cs.attrs = static_cast<uint16_t>(cs.attrs | ATTR_BOLD);
                            add(i, l.cl, e, t, cs);
                        }
                    }
                    Style bar = callout ? callout_style(*callout) : quote_st;
                    bar.attrs = 0;
                    for(size_t k = l.tl; k < l.me; ++k)
                        if(src[k] == '>') add(i, k, k + 1, "▋", bar);
                    break;
                }
                case MdBlock::Bullet: {
                    size_t indent = 0;
                    for(size_t k = l.lo; k < l.tl; ++k)
                        indent += src[k] == '\t' ? 4 : 1;
                    while(!indents.empty() && indents.back() > indent)
                        indents.pop_back();
                    if(indents.empty() || indents.back() < indent)
                        indents.push_back(indent);
                    const size_t depth = indents.size() - 1;

                    const std::string_view rest = src.substr(l.cl, l.ch - l.cl);
                    const bool box =
                        rest.size() >= 3 && rest[0] == '[' && rest[2] == ']' &&
                        (rest[1] == ' ' || rest[1] == 'x' || rest[1] == 'X') &&
                        (rest.size() == 3 || rest[3] == ' ');
                    if(box) {
                        const bool done = rest[1] != ' ';
                        const Style cs = md_style(MdRole::Checkbox, done);
                        if(l.level == 0) conceal_range(i, l.tl, l.cl);
                        add(i, l.cl, l.cl + 3,
                            nf ? (done ? "\U000f0c52" : "\U000f0131")
                               : (done ? "☑" : "☐"),
                            Style{.fg = cs.fg});
                    } else if(l.level == 0) {
                        add(i, l.tl, l.me, std::string(kBullets[depth % 4]),
                            Style{.fg = md_style(MdRole::Bullet).fg});
                    }
                    break;
                }
            }
        }
    }

    void conceal_range(size_t li, size_t lo, size_t hi) { add(li, lo, hi, {}); }

    // pipe tables: borders only, cells keep their source width so columns
    // stay aligned
    void tables() {
        const Style bord = theme_ui("md.table.border", Style{.fg = 0x585b70});
        std::vector<std::pair<size_t, size_t>> code;
        for(const MdSpan& s : d.spans)
            if(s.role == MdRole::Code) code.emplace_back(s.lo, s.hi);
        const auto in_code = [&](size_t p) {
            return std::ranges::any_of(
                code, [&](const auto& r) { return p >= r.first && p < r.second; });
        };
        const auto row_ok = [&](size_t i) {
            const MdLine& l = d.lines[i];
            return l.kind == MdBlock::Text && skip[i] != 2 &&
                   src.substr(l.tl, l.th - l.tl).find('|') != std::string_view::npos;
        };
        for(size_t i = 0; i + 1 < d.lines.size(); ++i) {
            if(!row_ok(i) || !row_ok(i + 1)) continue;
            const MdLine& dl = d.lines[i + 1];
            if(!is_delim_row(src.substr(dl.tl, dl.th - dl.tl))) continue;
            size_t last = i + 1;
            while(last + 1 < d.lines.size() && row_ok(last + 1)) ++last;
            heads.emplace_back(d.lines[i].lo, d.lines[i].hi);
            for(size_t r = i; r <= last; ++r) {
                const MdLine& l = d.lines[r];
                skip[r] = 1;
                if(r == i + 1) {  // delimiter row -> ├──┼──┤
                    for(size_t k = l.tl; k < l.th; ++k) {
                        if(src[k] == '|') {
                            add(r, k, k + 1,
                                k == l.tl ? "├" : k + 1 == l.th ? "┤" : "┼",
                                bord);
                        } else if(src[k] == '-') {
                            size_t e = k;
                            std::string t;
                            while(e < l.th && src[e] == '-') {
                                t += "─";
                                ++e;
                            }
                            add(r, k, e, t, bord);
                            k = e - 1;
                        } else if(src[k] == ':') {
                            add(r, k, k + 1, "━", bord);
                        }
                    }
                    continue;
                }
                for(size_t k = l.tl; k < l.th; ++k)
                    if(src[k] == '|' && !in_code(k) && (k == l.tl || src[k - 1] != '\\'))
                        add(r, k, k + 1, "│", bord);
            }
            i = last;
        }
    }

    void inlines() {
        struct Open {
            size_t lo, hi;
            std::string_view url;
        };
        std::vector<Open> opens;
        const Style link_st{.fg = md_style(MdRole::Link).fg};
        for(const MdSpan& s : d.spans) {
            if(s.hi <= s.lo) continue;
            const size_t li = line_at(s.lo);
            if(skip[li]) continue;
            switch(s.role) {
                case MdRole::Code:
                case MdRole::Strong:
                case MdRole::Emph:
                case MdRole::Strike:
                    if(s.marker) conceal(s.lo, s.hi);
                    break;
                case MdRole::Link:
                    if(!s.marker) break;
                    if(src[s.lo] == ']') conceal(s.lo, s.hi);
                    else opens.push_back({s.lo, s.hi, {}});
                    break;
                case MdRole::LinkUrl:
                    if(!s.marker) {
                        if(!opens.empty())
                            opens.back().url = src.substr(s.lo, s.hi - s.lo);
                        conceal(s.lo, s.hi);
                        break;
                    }
                    conceal(s.lo, s.hi);  // the closing ")"
                    if(!opens.empty()) {
                        const Open o = opens.back();
                        opens.pop_back();
                        const std::string ic =
                            link_icon(o.url, src[o.lo] == '!');
                        if(ic.empty()) conceal(o.lo, o.hi);
                        else add(line_at(o.lo), o.lo, o.hi, ic, link_st);
                    }
                    break;
                default: break;
            }
        }
    }
};

void apply(Buffer& b, const MdDoc& d, NsId ns) {
    const Style marker = theme_ui("md.marker", Style{.attrs = ATTR_DIM});
    for(const MdSpan& s : d.spans) {
        if(s.hi <= s.lo) continue;
        SpanDeco sd{};
        sd.start = s.lo;
        sd.end = s.hi;
        sd.ns = ns;
        if(s.fill) {
            sd.prio = kPrioFill;
            sd.st = md_fill_style(s.role, s.level);
            sd.fill = true;
        } else if(s.marker && dim_marker(s.role)) {
            sd.prio = kPrioMarker;
            sd.st = marker;
            if(s.role == MdRole::Heading)  // keep the heading colour
                sd.st.fg = md_style(s.role, s.level).fg;
        } else {
            sd.prio = inline_role(s.role) ? kPrioInline : kPrioBlock;
            sd.st = md_style(s.role, s.level);
        }
        b.decos.add_span(sd);
    }
}

}  // namespace

void md_build_view(std::string_view src, const MdDoc& d, MdState& m,
                   std::vector<std::pair<size_t, size_t>>* heads) {
    ViewBuilder vb(src, d);
    vb.frontmatter();
    vb.blocks();
    vb.tables();
    vb.inlines();
    m.line_first.clear();
    m.edits.clear();
    m.line_first.reserve(vb.per.size() + 1);
    for(std::vector<MdEdit>& v : vb.per) {
        m.line_first.push_back(static_cast<uint32_t>(m.edits.size()));
        std::ranges::stable_sort(v, {}, &MdEdit::lo);
        uint32_t end = 0;
        for(MdEdit& e : v) {
            if(e.lo < end) continue;  // overlaps the previous edit: drop it
            end = e.hi;
            m.edits.push_back(std::move(e));
        }
    }
    m.line_first.push_back(static_cast<uint32_t>(m.edits.size()));
    if(heads) *heads = std::move(vb.heads);
}

bool md_line_view(int row, std::string_view real, int width, SubstView& out) {
    if(!g_on || row < 0 || insertish(E.mode)) return false;
    if(real.size() > 4096) return false;
    Buffer& b = E.buf();
    md_buffer_refresh(b);
    const MdState& m = b.md;
    if(!m.applied || static_cast<size_t>(row) + 1 >= m.line_first.size())
        return false;
    const uint32_t a = m.line_first[static_cast<size_t>(row)];
    const uint32_t z = m.line_first[static_cast<size_t>(row) + 1];
    if(a == z) return false;
    out.text.clear();
    out.marks.clear();
    out.edits.clear();
    size_t copied = 0;
    for(uint32_t k = a; k < z; ++k) {
        const MdEdit& e = m.edits[k];
        if(e.lo < copied || e.hi > real.size()) continue;  // stale
        out.text.append(real.substr(copied, e.lo - copied));
        const size_t nlo = out.text.size();
        if(e.fill) {
            const int n = std::max(1, width - text_width(out.text));
            for(int i = 0; i < n; ++i) out.text += e.text;
        } else {
            out.text += e.text;
        }
        out.edits.push_back({e.lo, e.hi, nlo, out.text.size()});
        if(out.text.size() > nlo)
            out.marks.push_back({nlo, out.text.size(), kPrioView, e.st});
        copied = e.hi;
    }
    if(out.edits.empty()) return false;
    out.text.append(real.substr(copied));
    return true;
}

void md_set_enabled(bool on) { g_on = on; }
bool md_enabled() { return g_on; }

void md_buffer_refresh(Buffer& b) {
    MdState& m = b.md;
    if(m.fname != b.filename) {
        m.fname = b.filename;
        m.is_md = Syntax::detect(b.filename) == "markdown";
    }
    const bool want = g_on && m.is_md && b.pt.size() <= kMaxBytes;
    if(!want && !m.applied) return;

    const uint64_t rev = b.pt.revision(), th = theme_rev();
    // a buffer reload clears every deco without bumping our key: spans we
    // know we added but that are gone mean "rebuild"
    const bool wiped = m.spans > 0 && b.decos.empty();
    if(want && m.applied && m.rev == rev && m.theme == th && !wiped) return;

    static const NsId ns = deco_ns("markdown");
    b.sync_anchors();  // flush pending deco shifts BEFORE adding new spans
    b.decos.clear_ns(ns);
    m.applied = want;
    m.rev = rev;
    m.theme = th;
    m.spans = 0;
    m.line_first.clear();
    m.edits.clear();
    if(!want) return;

    const std::string text = b.pt.text();
    const MdDoc d = md_parse(text);
    apply(b, d, ns);
    m.spans = d.spans.size();
    std::vector<std::pair<size_t, size_t>> heads;
    md_build_view(text, d, m, &heads);
    const Style head = theme_ui("md.table.head",
                                Style{.fg = 0xf5c2e7, .attrs = ATTR_BOLD});
    for(const auto& [lo, hi] : heads) {
        SpanDeco sd{};
        sd.start = lo;
        sd.end = hi;
        sd.ns = ns;
        sd.prio = kPrioBlock;
        sd.st = head;
        b.decos.add_span(sd);
        ++m.spans;
    }
}

void markdown_register_commands() {
    command_add({.name = "MarkdownRender",
                 .desc = "Style markdown buffers: headings, code blocks, "
                         "quotes, lists, links",
                 .usage = "[on|off|toggle]",
                 .nargs = Nargs::Opt,
                 .run = [](const CmdLine& c) {
                     const std::string a = c.argv.empty() ? "toggle" : c.argv[0];
                     if(a == "on") g_on = true;
                     else if(a == "off") g_on = false;
                     else if(a == "toggle") g_on = !g_on;
                     else {
                         editor_set_status_message(
                             "MarkdownRender: on, off or toggle");
                         return;
                     }
                     editor_set_status_message("markdown render: {}",
                                               g_on ? "on" : "off");
                 }});
}
