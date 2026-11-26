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

// Buffer backend: decorates markdown buffers from md_parse().
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "columns.hpp"
#include "command.hpp"
#include "editor.hpp"
#include "glyphs.hpp"
#include "grapheme.hpp"
#include "markdown.hpp"
#include "normal.hpp"
#include "syntax.hpp"
#include "theme.hpp"
#include "virt.hpp"

namespace {

bool g_on = true;
bool g_reveal = false;  // raw source on the cursor line outside insert mode
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

    // The converter's anchor tag: a trailing " {#id}" on a heading or term
    // line ("# std::vector {#firstHeading}", "**T** {#T}"). find_anchor and
    // link jumps need it in the file, a reader does not need to see it.
    void conceal_anchor(size_t li, size_t lo, size_t hi) {
        const std::string_view t = src.substr(lo, hi - lo);
        if(t.size() < 5 || t.back() != '}') return;
        const size_t at = t.rfind(" {#");
        if(at == std::string_view::npos) return;
        const std::string_view id = t.substr(at + 3, t.size() - at - 4);
        if(id.empty() || id.find_first_of(" {}") != std::string_view::npos) return;
        add(li, lo + at, hi, {});
    }

    // "(since C++17)", "(until C++11)", "(removed in C++20)" ... end index of
    // the group starting at s[i] == '(', or 0.
    static size_t version_end(std::string_view s, size_t i) {
        static constexpr std::string_view kLead[] = {
            "(since ", "(until ", "(removed in ", "(deprecated in ",
            "(constexpr since "};
        bool ok = false;
        for(const std::string_view k : kLead)
            if(s.compare(i, k.size(), k) == 0) ok = true;
        if(!ok) return 0;
        const size_t close = s.find(')', i);
        if(close == std::string_view::npos || close - i > 48) return 0;
        if(s.substr(i, close - i).find_first_of("[]`") != std::string_view::npos)
            return 0;
        return close + 1;
    }

    // Version tags read as annotations, not prose: restyle them in place.
    void versions() {
        const Style st = theme_ui("md.version", Style{.fg = 0x6c7086, .attrs = ATTR_ITALIC});
        std::vector<std::pair<size_t, size_t>> code;  // inline `code` content
        for(const MdSpan& sp : d.spans)
            if(sp.role == MdRole::Code && !sp.marker) code.emplace_back(sp.lo, sp.hi);
        for(size_t i = 0; i < d.lines.size(); ++i) {
            const MdLine& l = d.lines[i];
            if(skip[i]) continue;
            if(l.kind != MdBlock::Text && l.kind != MdBlock::Bullet &&
               l.kind != MdBlock::Quote)
                continue;
            for(size_t p = src.find('(', l.cl); p < l.ch && p != std::string_view::npos;
                p = src.find('(', p + 1)) {
                const size_t e = version_end(src.substr(0, l.ch), p);
                if(!e) continue;
                const auto it = std::ranges::upper_bound(code, p, {}, &std::pair<size_t, size_t>::first);
                if(it != code.begin() && p < std::prev(it)->second) continue;
                add(i, p, e, std::string(src.substr(p, e - p)), st);
                p = e - 1;
            }
        }
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
                case MdBlock::Text:
                    if(src.compare(l.tl, 2, "**") == 0) conceal_anchor(i, l.tl, l.th);
                    break;
                case MdBlock::Heading: {
                    conceal_anchor(i, l.cl, l.ch);
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
                        md_box_state(rest[1]) >= 0 &&
                        (rest.size() == 3 || rest[3] == ' ');
                    if(box) {
                        const int st = md_box_state(rest[1]);
                        const Style cs = md_style(MdRole::Checkbox, st);
                        static constexpr std::string_view kNf[4] = {
                            "\U000f0131", "\U000f0c52", "\U000f081b",
                            "\U000f0856"};
                        static constexpr std::string_view kPlain[4] = {
                            "☐", "☑", "☒", "◩"};
                        if(l.level == 0) conceal_range(i, l.tl, l.cl);
                        add(i, l.cl, l.cl + 3,
                            std::string(nf ? kNf[st] : kPlain[st]),
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

    std::vector<MdTable> tbls;

    // pipe tables: the pipes become box glyphs here (same width, so they are
    // width independent); cell padding and truncation need the terminal
    // width and are decided per frame by table_edits().
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
        const auto blank = [&](size_t k) { return src[k] == ' ' || src[k] == '\t'; };
        for(size_t i = 0; i + 1 < d.lines.size(); ++i) {
            if(!row_ok(i) || !row_ok(i + 1)) continue;
            const MdLine& dl = d.lines[i + 1];
            if(!is_delim_row(src.substr(dl.tl, dl.th - dl.tl))) continue;
            size_t last = i + 1;
            while(last + 1 < d.lines.size() && row_ok(last + 1)) ++last;
            heads.emplace_back(d.lines[i].lo, d.lines[i].hi);
            MdTable t;
            for(size_t r = i; r <= last; ++r) {
                const MdLine& l = d.lines[r];
                MdTableRow row;
                row.line = static_cast<uint32_t>(r);
                row.delim = r == i + 1;
                std::vector<size_t> P;  // unescaped pipes outside code spans
                for(size_t k = l.tl; k < l.th; ++k)
                    if(src[k] == '|' && !in_code(k) &&
                       (k == l.tl || src[k - 1] != '\\'))
                        P.push_back(k);
                const bool lead = !P.empty() && P.front() == l.tl;
                row.trail = !P.empty() && P.back() + 1 == l.th &&
                            (P.size() > 1 || !lead);
                const auto cell = [&](size_t cs, size_t ce) {
                    MdCell c;
                    c.cs = static_cast<uint32_t>(cs - l.lo);
                    c.ce = static_cast<uint32_t>(ce - l.lo);
                    size_t ts = cs, te = ce;
                    while(ts < te && blank(ts)) ++ts;
                    while(te > ts && blank(te - 1)) --te;
                    if(ts == te) ts = te = cs;  // empty: all padding
                    c.ts = static_cast<uint32_t>(ts - l.lo);
                    c.te = static_cast<uint32_t>(te - l.lo);
                    row.cells.push_back(c);
                };
                size_t start = lead ? P[0] + 1 : l.tl;
                for(size_t k = lead ? 1 : 0; k < P.size(); ++k) {
                    cell(start, P[k]);
                    start = P[k] + 1;
                }
                if(!row.trail) cell(start, l.th);
                for(const size_t k : P) {
                    const char* g = "│";
                    if(row.delim)
                        g = k == l.tl ? "├" : k + 1 == l.th ? "┤" : "┼";
                    add(r, k, k + 1, g, bord);
                }
                t.edge = std::max(t.edge, int(lead) + int(row.trail));
                t.rows.push_back(std::move(row));
            }
            const size_t n = t.rows[0].cells.size();
            t.nat.assign(n, 3);
            t.align.assign(n, 'l');
            const MdTableRow& dr = t.rows[1];
            for(size_t c = 0; c < n && c < dr.cells.size(); ++c) {
                const MdLine& l = d.lines[dr.line];
                const std::string_view x = src.substr(
                    l.lo + dr.cells[c].ts, dr.cells[c].te - dr.cells[c].ts);
                const bool lc = x.starts_with(':'), rc = x.ends_with(':');
                t.align[c] = lc && rc ? 'c' : rc ? 'r' : 'l';
            }
            tbls.push_back(std::move(t));
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

// Syntax colours for fenced blocks. Each body is highlighted as one document
// (multi-line tokens work) and laid over the code-block fill. Spans are
// source-relative because the body is the exact slice of the buffer.
void highlight_fences(std::string_view src, const MdDoc& d, MdState& m,
                      Buffer& b, NsId ns) {
    std::map<std::string, std::vector<Span>> next;
    bool open = false;
    for(size_t i = 0; i < d.lines.size(); ++i) {
        const MdLine& f = d.lines[i];
        if(f.kind != MdBlock::Fence) continue;
        open = !open;
        if(!open) continue;  // closing fence
        size_t j = i + 1;
        while(j < d.lines.size() && d.lines[j].kind == MdBlock::CodeLine) ++j;
        const std::string lang = fence_info(src.substr(f.tl, f.th - f.tl));
        if(lang.empty() || j == i + 1) continue;
        const size_t lo = d.lines[i + 1].lo, hi = d.lines[j - 1].hi;
        std::string key = lang + '\n';
        key.append(src.substr(lo, hi - lo));
        auto it = next.find(key);
        if(it == next.end()) {
            auto old = m.hl.find(key);
            if(old != m.hl.end()) {
                it = next.emplace(key, std::move(old->second)).first;
            } else {
                const std::string_view body = src.substr(lo, hi - lo);
                auto sp = syntax_highlight(lang, body);
                it = next.emplace(key, sp ? std::move(*sp)
                                          : std::vector<Span>{}).first;
            }
        }
        for(const Span& s : it->second) {
            if(s.end <= s.start || lo + s.end > hi) continue;
            SpanDeco sd{};
            sd.start = lo + s.start;
            sd.end = lo + s.end;
            sd.ns = ns;
            sd.prio = kPrioBlock;
            sd.st = s.st;
            b.decos.add_span(sd);
            ++m.spans;
        }
    }
    m.hl = std::move(next);
}

}  // namespace

void md_build_view(std::string_view src, const MdDoc& d, MdState& m,
                   std::vector<std::pair<size_t, size_t>>* heads) {
    ViewBuilder vb(src, d);
    vb.frontmatter();
    vb.blocks();
    vb.tables();
    vb.inlines();
    vb.versions();
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

    // tables: cell widths as drawn (inline markers concealed), then the
    // natural column widths
    m.tables = std::move(vb.tbls);
    m.line_tbl.assign(d.lines.size(), -1);
    for(size_t t = 0; t < m.tables.size(); ++t) {
        MdTable& tb = m.tables[t];
        for(MdTableRow& r : tb.rows) {
            m.line_tbl[r.line] = static_cast<int32_t>(t);
            if(r.delim) continue;
            const MdLine& l = d.lines[r.line];
            const std::string_view real = src.substr(l.lo, l.hi - l.lo);
            const uint32_t a = m.line_first[r.line], z = m.line_first[r.line + 1];
            for(size_t c = 0; c < r.cells.size(); ++c) {
                MdCell& k = r.cells[c];
                std::string s;
                size_t at = k.ts;
                for(uint32_t q = a; q < z; ++q) {
                    const MdEdit& e = m.edits[q];
                    if(e.lo < k.ts || e.hi > k.te) continue;
                    s.append(real.substr(at, e.lo - at));
                    s += e.text;
                    at = e.hi;
                }
                s.append(real.substr(at, k.te - at));
                k.w = text_width(s);
                if(c < tb.nat.size()) tb.nat[c] = std::max(tb.nat[c], k.w);
            }
        }
    }
}

namespace {

// The line under the cursor (and every line a visual selection touches)
// shows its raw source, as render-markdown does: you edit what is in the
// file, and the cursor column is the file's column.
bool line_revealed(int row) {
    if(!g_reveal) return false;
    if(row == E.cy) return true;
    if(E.mode == VISUAL) {
        const PieceTable& pt = E.buf().pt;
        const size_t r = static_cast<size_t>(row);
        const size_t ls = pt.line_start(r), le = pt.line_end(r);
        const Range sel = visual_range();
        return sel.end > ls && sel.start <= le;
    }
    return false;
}

// Every edit must start and end on a grapheme-cluster boundary of the source
// (a marker glued to a combining mark must stay raw) and, once spliced, the
// pieces around it must not fuse into a new cluster (a flag split by `**`).
bool edits_on_boundaries(std::string_view real, const SubstView& v) {
    for(const SubstView::Edit& e : v.edits) {
        if(gr_snap(real, e.lo) != e.lo) return false;
        if(e.hi < real.size() && gr_snap(real, e.hi) != e.hi) return false;
        if(e.nlo < v.text.size() && gr_snap(v.text, e.nlo) != e.nlo) return false;
        if(e.nhi < v.text.size() && gr_snap(v.text, e.nhi) != e.nhi) return false;
    }
    return true;
}

}  // namespace

namespace {

constexpr int kMinCol = 3;  // narrowest a shrunk column gets (one char + "…")

// Column content widths for a terminal width: natural widths, with the widest
// column shrunk one cell at a time until the row fits. Empty = cannot fit.
const std::vector<int>& table_layout(const MdTable& t, int width) {
    if(t.lay_w == width) return t.cw;
    t.lay_w = width;
    t.cw = t.nat;
    const int n = static_cast<int>(t.cw.size());
    int total = t.edge + (n - 1);
    for(const int w : t.cw) total += w + 2;
    while(total > width) {
        auto it = std::ranges::max_element(t.cw);
        if(it == t.cw.end() || *it <= kMinCol) {
            t.cw.clear();
            break;
        }
        --*it;
        --total;
    }
    return t.cw;
}

std::string pad(int n) { return std::string(static_cast<size_t>(std::max(n, 0)), ' '); }

// Padding, alignment and truncation of one table row at the layout's widths.
// Cells wider than their column lose their inline concealing (raw text is cut
// at a grapheme boundary and ends in "…"), so a row is always one screen row.
void table_edits(const MdTable& t, size_t ri, std::string_view real,
                 const std::vector<int>& cw, std::vector<MdEdit>& ed) {
    static const Style bord = theme_ui("md.table.border", Style{.fg = 0x585b70});
    const MdTableRow& r = t.rows[ri];
    const auto put = [&](size_t lo, size_t hi, std::string text, Style st = {}) {
        if(hi > real.size() || real.substr(lo, hi - lo) == text) return;
        st.attrs = static_cast<uint16_t>(st.attrs | ATTR_NODIM);
        ed.push_back({static_cast<uint32_t>(lo), static_cast<uint32_t>(hi),
                      std::move(text), st, false});
    };
    for(size_t c = 0; c < r.cells.size() && c < cw.size(); ++c) {
        const MdCell& k = r.cells[c];
        const int w = cw[c];
        if(r.delim) {
            std::string dash;
            for(int i = 0; i < w + 2; ++i) dash += "─";
            put(k.cs, k.ce, std::move(dash), bord);
            continue;
        }
        const bool last_open = c + 1 == r.cells.size() && !r.trail;
        std::string lead, tail;
        if(k.w > w) {  // truncate the raw text
            int used = 0;
            size_t at = k.ts;
            while(at < k.te) {
                const size_t e = gr_next(real, at);
                const int cwid = cluster_width(real, at, e, used);
                if(used + cwid > w - 1) break;
                used += cwid;
                at = e;
            }
            std::erase_if(ed, [&](const MdEdit& e) {
                return e.lo >= k.ts && e.hi <= k.te;
            });
            lead = " ";
            tail = "…" + (last_open ? std::string() : pad(w - 1 - used) + " ");
            put(k.cs, k.ts, lead);
            put(at, k.ce, tail, Style{.fg = 0x6c7086});
            continue;
        }
        const int gap = w - k.w;
        const int before = t.align[std::min(c, t.align.size() - 1)] == 'r'   ? gap
                           : t.align[std::min(c, t.align.size() - 1)] == 'c' ? gap / 2
                                                                             : 0;
        lead = " " + pad(before);
        tail = last_open ? std::string() : pad(gap - before) + " ";
        put(k.cs, k.ts, std::move(lead));
        put(k.te, k.ce, std::move(tail));
    }
}

}  // namespace

bool md_line_view(int row, std::string_view real, int width, SubstView& out) {
    if(!g_on || row < 0 || insertish(E.mode)) return false;
    if(real.size() > 4096) return false;
    if(line_revealed(row)) return false;
    Buffer& b = E.buf();
    md_buffer_refresh(b);
    const MdState& m = b.md;
    if(!m.applied || static_cast<size_t>(row) + 1 >= m.line_first.size())
        return false;
    const uint32_t a = m.line_first[static_cast<size_t>(row)];
    const uint32_t z = m.line_first[static_cast<size_t>(row) + 1];
    const MdEdit* eb = m.edits.data() + a;
    size_t en = z - a;
    std::vector<MdEdit> merged;  // table rows: stored edits + this width's
    const size_t ur = static_cast<size_t>(row);
    if(ur < m.line_tbl.size() && m.line_tbl[ur] >= 0) {
        const MdTable& tb = m.tables[static_cast<size_t>(m.line_tbl[ur])];
        const std::vector<int>& cw = table_layout(tb, width);
        if(!cw.empty()) {
            merged.assign(eb, eb + en);
            table_edits(tb, ur - tb.rows[0].line, real, cw, merged);
            // an insertion sorts before an edit that starts at the same byte
            std::ranges::stable_sort(merged, {}, [](const MdEdit& e) {
                return std::pair(e.lo, e.hi > e.lo);
            });
            eb = merged.data();
            en = merged.size();
        }
    }
    if(en == 0) return false;
    out.text.clear();
    out.marks.clear();
    out.edits.clear();
    out.atomic = true;
    size_t copied = 0;
    for(size_t k = 0; k < en; ++k) {
        const MdEdit& e = eb[k];
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
    return edits_on_boundaries(real, out);
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
    m.tables.clear();
    m.line_tbl.clear();
    if(!want) return;

    const std::string text = b.pt.text();
    const MdDoc d = md_parse(text);
    apply(b, d, ns);
    m.spans = d.spans.size();
    highlight_fences(text, d, m, b, ns);
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

void md_set_reveal(bool on) { g_reveal = on; }
bool md_reveal() { return g_reveal; }

void markdown_register_commands() {
    command_add({.name = "MarkdownReveal",
                 .desc = "Show raw markdown on the cursor line (and the "
                         "visual selection) instead of the concealed view",
                 .usage = "[on|off|toggle]",
                 .nargs = Nargs::Opt,
                 .run = [](const CmdLine& c) {
                     const std::string a = c.argv.empty() ? "toggle" : c.argv[0];
                     if(a == "on") g_reveal = true;
                     else if(a == "off") g_reveal = false;
                     else if(a == "toggle") g_reveal = !g_reveal;
                     else {
                         editor_set_status_message(
                             "MarkdownReveal: on, off or toggle");
                         return;
                     }
                     editor_set_status_message("markdown reveal: {}",
                                               g_reveal ? "on" : "off");
                 }});
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
