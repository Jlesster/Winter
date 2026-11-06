#include "markdown.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>
#include "columns.hpp"
#include "syntax.hpp"
#include "theme.hpp"
#include "utf8.hpp"

namespace {

bool sp(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// ---- block pass ----------------------------------------------------------

bool is_rule(std::string_view s) {
    return s.size() >= 3 && (s[0] == '-' || s[0] == '*' || s[0] == '_') &&
           s.find_first_not_of(s[0]) == std::string_view::npos;
}

void classify(std::string_view src, MdLine& l) {
    const std::string_view s = src.substr(l.tl, l.th - l.tl);
    l.kind = MdBlock::Text;
    l.me = l.tl;
    l.cl = l.tl;
    l.ch = l.th;
    if(is_rule(s)) {
        l.kind = MdBlock::Rule;
        return;
    }
    if(s[0] == '#') {  // 1-6 #s then a space or the end of the line
        const size_t n = s.find_first_not_of('#');
        const size_t hashes = n == std::string_view::npos ? s.size() : n;
        if(hashes <= 6 && (hashes == s.size() || s[hashes] == ' ')) {
            size_t c = l.tl + hashes;
            l.me = c;
            while(c < l.th && src[c] == ' ') ++c;
            l.kind = MdBlock::Heading;
            l.level = static_cast<uint8_t>(hashes);
            l.cl = c;
            return;
        }
    }
    if(s[0] == '>') {
        size_t c = l.tl;
        uint8_t depth = 0;
        while(c < l.th && src[c] == '>') {
            ++c;
            ++depth;
            if(c < l.th && src[c] == ' ') ++c;
        }
        l.kind = MdBlock::Quote;
        l.level = depth;
        l.me = l.cl = c;
        return;
    }
    size_t m = 0;  // bullet marker length
    bool ordered = false;
    if((s[0] == '-' || s[0] == '*' || s[0] == '+') && s.size() >= 2 &&
       s[1] == ' ') {
        m = 1;
    } else if(std::isdigit(static_cast<unsigned char>(s[0]))) {
        size_t d = 0;
        while(d < s.size() && d < 9 &&
              std::isdigit(static_cast<unsigned char>(s[d])))
            ++d;
        if(d < s.size() && (s[d] == '.' || s[d] == ')') && d + 1 < s.size() &&
           s[d + 1] == ' ') {
            m = d + 1;
            ordered = true;
        }
    }
    if(m) {
        size_t c = l.tl + m;
        l.me = c;
        while(c < l.th && src[c] == ' ') ++c;
        l.kind = MdBlock::Bullet;
        l.level = ordered ? 1 : 0;
        l.cl = c;
    }
}

size_t fill_hi(std::string_view src, const MdLine& l) {
    size_t h = l.hi;
    if(h < src.size() && src[h] == '\r') ++h;
    if(h < src.size() && src[h] == '\n') ++h;
    return h;
}

// ---- inline pass ---------------------------------------------------------

size_t run_len(std::string_view s, size_t i, size_t hi, char c) {
    size_t n = 0;
    while(i + n < hi && s[i + n] == c) ++n;
    return n;
}

// End (exclusive) of the code span opening at `i`, or npos.
size_t code_end(std::string_view s, size_t i, size_t hi, size_t n) {
    size_t j = i + n;
    while(j < hi) {
        if(s[j] != '`') {
            ++j;
            continue;
        }
        const size_t m = run_len(s, j, hi, '`');
        if(m == n) return j;
        j += m;
    }
    return std::string_view::npos;
}

void inline_pass(std::string_view s, size_t lo, size_t hi, int depth,
                 std::vector<MdSpan>& out) {
    constexpr size_t npos = std::string_view::npos;
    if(lo >= hi || depth > 4 || hi - lo > 4000) return;
    const auto punct = [](char c) {
        return std::ispunct(static_cast<unsigned char>(c)) != 0;
    };
    const auto alnum = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0;
    };
    size_t i = lo;
    while(i < hi) {
        const char c = s[i];
        if(c == '\\' && i + 1 < hi && punct(s[i + 1])) {
            i += 2;
            continue;
        }
        if(c == '`') {
            const size_t n = run_len(s, i, hi, '`');
            const size_t j = code_end(s, i, hi, n);
            if(j == npos) {
                i += n;
                continue;
            }
            out.push_back({i, i + n, MdRole::Code, 0, true});
            if(j > i + n) out.push_back({i + n, j, MdRole::Code});
            out.push_back({j, j + n, MdRole::Code, 0, true});
            i = j + n;
            continue;
        }
        if(c == '[' || (c == '!' && i + 1 < hi && s[i + 1] == '[')) {
            const size_t open = c == '!' ? i + 1 : i;
            int d = 0;
            size_t j = open;
            for(; j < hi; ++j) {
                if(s[j] == '\\') {
                    ++j;
                    continue;
                }
                if(s[j] == '[') ++d;
                else if(s[j] == ']' && --d == 0) break;
            }
            if(j + 1 < hi && s[j + 1] == '(') {
                int pd = 1;
                size_t k = j + 2;
                for(; k < hi; ++k) {
                    if(s[k] == '\\') {
                        ++k;
                        continue;
                    }
                    if(s[k] == '(') ++pd;
                    else if(s[k] == ')' && --pd == 0) break;
                }
                if(k < hi) {
                    out.push_back({i, open + 1, MdRole::Link, 0, true});
                    if(j > open + 1) {
                        out.push_back({open + 1, j, MdRole::Link});
                        inline_pass(s, open + 1, j, depth + 1, out);
                    }
                    out.push_back({j, j + 2, MdRole::Link, 0, true});
                    if(k > j + 2) out.push_back({j + 2, k, MdRole::LinkUrl});
                    out.push_back({k, k + 1, MdRole::LinkUrl, 0, true});
                    i = k + 1;
                    continue;
                }
            }
            ++i;
            continue;
        }
        if(c == '*' || c == '_' || c == '~') {
            const size_t n = run_len(s, i, hi, c);
            const bool ok = c == '~' ? n == 2 : n <= 3;
            const bool open_ok =
                ok && i + n < hi && !sp(s[i + n]) &&
                !(c == '_' && i > lo && alnum(s[i - 1]));
            if(!open_ok) {
                i += n;
                continue;
            }
            size_t close = npos;
            for(size_t j = i + n; j < hi;) {
                if(s[j] == '\\') {
                    j += 2;
                    continue;
                }
                if(s[j] == '`') {
                    const size_t m = run_len(s, j, hi, '`');
                    const size_t e = code_end(s, j, hi, m);
                    j = e == npos ? j + m : e + m;
                    continue;
                }
                if(s[j] == c) {
                    const size_t m = run_len(s, j, hi, c);
                    const bool flank =
                        j > i + n && !sp(s[j - 1]) &&
                        !(c == '_' && j + m < hi && alnum(s[j + m]));
                    if(m == n && flank) {
                        close = j;
                        break;
                    }
                    j += m;
                    continue;
                }
                ++j;
            }
            if(close == npos) {
                i += n;
                continue;
            }
            const size_t cs = i + n;
            if(c == '~') {
                out.push_back({i, cs, MdRole::Strike, 0, true});
                out.push_back({cs, close, MdRole::Strike});
                out.push_back({close, close + n, MdRole::Strike, 0, true});
            } else if(n == 1) {
                out.push_back({i, cs, MdRole::Emph, 0, true});
                out.push_back({cs, close, MdRole::Emph});
                out.push_back({close, close + n, MdRole::Emph, 0, true});
            } else if(n == 2) {
                out.push_back({i, cs, MdRole::Strong, 0, true});
                out.push_back({cs, close, MdRole::Strong});
                out.push_back({close, close + n, MdRole::Strong, 0, true});
            } else {  // ***both***
                out.push_back({i, i + 1, MdRole::Emph, 0, true});
                out.push_back({i + 1, cs, MdRole::Strong, 0, true});
                out.push_back({cs, close, MdRole::Strong});
                out.push_back({cs, close, MdRole::Emph});
                out.push_back({close, close + 2, MdRole::Strong, 0, true});
                out.push_back({close + 2, close + 3, MdRole::Emph, 0, true});
            }
            inline_pass(s, cs, close, depth + 1, out);
            i = close + n;
            continue;
        }
        ++i;
    }
}

}  // namespace

std::vector<MdLine> md_blocks(std::string_view src) {
    std::vector<MdLine> out;
    bool fence = false;
    size_t pos = 0;
    while(pos <= src.size()) {
        const size_t nl = src.find('\n', pos);
        size_t hi = nl == std::string_view::npos ? src.size() : nl;
        if(hi > pos && src[hi - 1] == '\r') --hi;
        MdLine l;
        l.lo = pos;
        l.hi = hi;
        pos = nl == std::string_view::npos ? src.size() + 1 : nl + 1;

        size_t a = l.lo;
        while(a < hi && sp(src[a])) ++a;
        size_t z = hi;
        while(z > a && sp(src[z - 1])) --z;
        l.tl = l.me = l.cl = a;
        l.th = l.ch = z;

        if(src.substr(a, hi - a).starts_with("```")) {
            fence = !fence;
            l.kind = MdBlock::Fence;
        } else if(fence) {
            l.kind = MdBlock::CodeLine;
        } else if(a == z) {
            l.kind = MdBlock::Blank;
        } else {
            classify(src, l);
        }
        out.push_back(l);
    }
    return out;
}

MdDoc md_parse(std::string_view src) {
    MdDoc d;
    d.lines = md_blocks(src);
    std::vector<MdSpan>& out = d.spans;
    for(const MdLine& l : d.lines) {
        switch(l.kind) {
            case MdBlock::Blank: break;
            case MdBlock::Fence:
                out.push_back({l.lo, fill_hi(src, l), MdRole::CodeBlock, 0,
                               false, true});
                out.push_back({l.tl, l.th, MdRole::CodeFence, 0, true});
                break;
            case MdBlock::CodeLine:
                out.push_back({l.lo, fill_hi(src, l), MdRole::CodeBlock, 0,
                               false, true});
                break;
            case MdBlock::Rule:
                out.push_back({l.tl, l.th, MdRole::Rule});
                break;
            case MdBlock::Heading:
                out.push_back({l.lo, fill_hi(src, l), MdRole::Heading, l.level,
                               false, true});
                out.push_back({l.tl, l.me, MdRole::Heading, l.level, true});
                if(l.ch > l.cl)
                    out.push_back({l.cl, l.ch, MdRole::Heading, l.level});
                inline_pass(src, l.cl, l.ch, 0, out);
                break;
            case MdBlock::Quote:
                out.push_back({l.tl, l.me, MdRole::Quote, l.level, true});
                if(l.ch > l.cl)
                    out.push_back({l.cl, l.ch, MdRole::Quote, l.level});
                inline_pass(src, l.cl, l.ch, 0, out);
                break;
            case MdBlock::Bullet: {
                out.push_back({l.tl, l.me, MdRole::Bullet, l.level, true});
                size_t c = l.cl;
                const std::string_view rest = src.substr(c, l.ch - c);
                if(rest.size() >= 3 && rest[0] == '[' && rest[2] == ']' &&
                   (rest[1] == ' ' || rest[1] == 'x' || rest[1] == 'X') &&
                   (rest.size() == 3 || rest[3] == ' ')) {
                    out.push_back({c, c + 3, MdRole::Checkbox,
                                   static_cast<uint8_t>(rest[1] != ' '),
                                   true});
                    c += 3;
                }
                inline_pass(src, c, l.ch, 0, out);
                break;
            }
            case MdBlock::Text: inline_pass(src, l.tl, l.th, 0, out); break;
        }
    }
    return d;
}

// ---- styles --------------------------------------------------------------

Style md_style(MdRole r, int lv) {
    switch(r) {
        case MdRole::Heading: {
            static constexpr uint32_t kHead[] = {
                0xcba6f7, 0xf38ba8, 0xfab387, 0xf9e2af,
                0xa6e3a1, 0x74c7ec, 0xcba6f7};
            const bool by_level = lv >= 1 && lv <= 6;
            Style s = theme_ui(
                "md.heading",
                Style{.fg = kHead[by_level ? lv : 0], .attrs = ATTR_BOLD});
            if(by_level) s = theme_ui(std::format("md.h{}", lv), s);
            return s;
        }
        case MdRole::Code: return theme_ui("md.code", Style{.fg = 0x89b4fa});
        case MdRole::CodeBlock: return theme_ui("md.code.block", Style{});
        case MdRole::CodeFence:
            return theme_ui("md.code.fence", Style{.fg = 0x6c7086});
        case MdRole::Strong:
            return theme_ui("md.strong", Style{.attrs = ATTR_BOLD});
        case MdRole::Emph:
            return theme_ui("md.emph", Style{.attrs = ATTR_ITALIC});
        case MdRole::Strike:
            return theme_ui("md.strike", Style{.attrs = ATTR_STRIKE});
        case MdRole::Link:
            return theme_ui("md.link",
                            Style{.fg = 0xb4befe, .attrs = ATTR_UNDERLINE});
        case MdRole::LinkUrl:
            return theme_ui("md.link.url", Style{.fg = 0x74c7ec});
        case MdRole::Quote:
            return theme_ui("md.quote",
                            Style{.fg = 0x9399b2, .attrs = ATTR_ITALIC});
        case MdRole::Bullet: return theme_ui("md.bullet", Style{.fg = 0xcba6f7});
        case MdRole::Checkbox:
            return lv ? theme_ui("md.checkbox.checked", Style{.fg = 0xa6e3a1})
                      : theme_ui("md.checkbox.unchecked",
                                 Style{.fg = 0xf9e2af});
        case MdRole::Rule: return theme_ui("md.rule", Style{.fg = 0x585b70});
    }
    return {};
}

Style md_fill_style(MdRole r, int) {
    switch(r) {
        case MdRole::Heading:
            return theme_ui("md.heading.bg", Style{.bg = 0x2a2b3d});
        case MdRole::CodeBlock:
            return theme_ui("md.code.block", Style{.bg = 0x181825});
        default: return {};
    }
}

// ---- float backend -------------------------------------------------------

namespace {

std::string fit_cells(std::string_view s, int w) {
    std::string out;
    int used = 0;
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        const int cw = cell_width(d.cp, 0);
        if(used + cw > w) break;
        out.append(s.substr(i, d.len));
        used += cw;
        i += d.len;
    }
    return out;
}

// Wraps one paragraph; `inline code` becomes Code segments, "\_" loses its "\".
void wrap_para(std::vector<MdRow>& out, std::string_view para, MdTone base,
               int w, size_t max) {
    struct Tok {
        std::string text;
        MdTone tone;
        bool gap;  // a space came before it
    };
    std::vector<Tok> toks;
    std::string word;
    bool code = false, gap = false;
    const auto flush = [&] {
        if(word.empty()) return;
        toks.push_back({std::move(word), code ? MdTone::Code : base, gap});
        word.clear();
        gap = false;
    };
    for(size_t i = 0; i < para.size(); ++i) {
        const char c = para[i];
        if(c == '`') {
            flush();
            code = !code;
        } else if(c == ' ') {
            flush();
            gap = true;
        } else if(c == '\\' && i + 1 < para.size() &&
                  std::ispunct(static_cast<unsigned char>(para[i + 1]))) {
            word += para[++i];
        } else {
            word += c;
        }
    }
    flush();

    MdRow cur;
    const auto emit = [&] {
        out.push_back(std::move(cur));
        cur = MdRow{};
    };
    for(Tok& t : toks) {
        if(out.size() >= max) return;
        std::string tx = fit_cells(t.text, w);
        const int tw = text_width(tx);
        int gapw = cur.width > 0 && t.gap ? 1 : 0;
        if(cur.width > 0 && cur.width + gapw + tw > w) {
            emit();
            gapw = 0;
            if(out.size() >= max) return;
        }
        if(gapw) cur.segs.push_back({" ", MdTone::Text});
        cur.segs.push_back({std::move(tx), t.tone});
        cur.width += gapw + tw;
    }
    if(cur.width > 0 && out.size() < max) emit();
}

// wrap_para with a hanging prefix: `first` leads the first row, `rest` (the
// same width) leads every following row. A bare marker with no text (an empty
// list item) still gets its row.
void wrap_prefixed(std::vector<MdRow>& out, std::string_view para,
                   MdTone base, int w, size_t max,
                   const std::vector<MdSeg>& first,
                   const std::vector<MdSeg>& rest) {
    int pw = 0;
    for(const MdSeg& g : first) pw += text_width(g.text);
    const size_t start = out.size();
    wrap_para(out, para, base, std::max(1, w - pw), max);
    if(out.size() == start && out.size() < max) out.emplace_back();
    for(size_t i = start; i < out.size(); ++i) {
        const std::vector<MdSeg>& pre = i == start ? first : rest;
        out[i].segs.insert(out[i].segs.begin(), pre.begin(), pre.end());
        out[i].width += pw;
    }
}

// Language word of a fence line: ```cpp, ~~~ rust, ```{.py} -> "cpp", "rust".
std::string fence_lang(std::string_view line) {
    size_t i = line.find_first_not_of("`~");
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

// Cuts [a, b) of a highlighted block into segments: styled where a span
// covers it, plain text elsewhere.
void hl_segs(MdRow& r, std::string_view code, size_t a, size_t b,
             const std::vector<Span>& spans) {
    size_t pos = a;
    auto it = std::ranges::lower_bound(spans, a, {}, &Span::end);
    const auto plain = [&](size_t hi) {
        if(hi > pos)
            r.segs.push_back({std::string(code.substr(pos, hi - pos)),
                              MdTone::Text, std::nullopt});
        pos = std::max(pos, hi);
    };
    for(; it != spans.end() && it->start < b; ++it) {
        const size_t s0 = std::max(it->start, a), e0 = std::min(it->end, b);
        if(s0 >= e0 || e0 <= pos) continue;
        plain(s0);
        r.segs.push_back({std::string(code.substr(pos, e0 - pos)),
                          MdTone::Text, it->st});
        pos = e0;
    }
    plain(b);
}

}  // namespace

std::vector<MdRow> md_float_layout(std::string_view text, int w, size_t max) {
    std::vector<MdRow> out;
    const std::vector<MdLine> lines = md_blocks(text);

    // Each fence body is highlighted as one document, so multi-line tokens
    // (block comments, raw strings) come out right. Blocks whose language is
    // unknown keep the flat code look.
    struct Hl {
        std::string code;  // the body, tabs as spaces, one '\n' per line
        std::vector<Span> spans;
    };
    std::vector<Hl> hls;
    std::vector<int> hl_of(lines.size(), -1);
    std::vector<size_t> hl_off(lines.size(), 0);
    for(size_t i = 0; i < lines.size(); ++i) {
        if(lines[i].kind != MdBlock::Fence) continue;
        size_t j = i + 1;
        std::string code;
        while(j < lines.size() && lines[j].kind == MdBlock::CodeLine) {
            hl_off[j] = code.size();
            code.append(text.substr(lines[j].lo, lines[j].hi - lines[j].lo));
            code += '\n';
            ++j;
        }
        if(j > i + 1) {  // a closing fence has no body after it
            std::ranges::replace(code, '\t', ' ');
            const std::string lang = fence_lang(
                text.substr(lines[i].tl, lines[i].th - lines[i].tl));
            if(auto sp = lang.empty() ? std::nullopt
                                      : syntax_highlight(lang, code)) {
                for(size_t k = i + 1; k < j; ++k)
                    hl_of[k] = static_cast<int>(hls.size());
                hls.push_back({std::move(code), std::move(*sp)});
            }
            i = j - 1;
        }
    }

    for(size_t li = 0; li < lines.size(); ++li) {
        const MdLine& l = lines[li];
        if(out.size() >= max) break;
        switch(l.kind) {
            case MdBlock::Fence: break;
            case MdBlock::CodeLine: {  // keeps indentation, long lines wrap
                std::string rest(text.substr(l.lo, l.hi - l.lo));
                std::ranges::replace(rest, '\t', ' ');
                const Hl* hl = hl_of[li] >= 0 ? &hls[hl_of[li]] : nullptr;
                size_t at = hl ? hl_off[li] : 0;  // offset of `rest` in hl->code
                while(!rest.empty() && out.size() < max) {
                    std::string chunk = fit_cells(rest, w);
                    if(chunk.empty()) break;
                    rest.erase(0, chunk.size());
                    MdRow r;
                    r.width = text_width(chunk);
                    if(hl) {
                        hl_segs(r, hl->code, at, at + chunk.size(), hl->spans);
                        at += chunk.size();
                    } else {
                        r.segs.push_back(
                            {std::move(chunk), MdTone::Code, std::nullopt});
                    }
                    out.push_back(std::move(r));
                }
                break;
            }
            case MdBlock::Blank:
                if(!out.empty() && !out.back().rule &&
                   !out.back().segs.empty())
                    out.emplace_back();
                break;
            case MdBlock::Rule:
                while(!out.empty() && !out.back().rule &&
                      out.back().segs.empty())
                    out.pop_back();  // no blank line above a rule
                if(!out.empty() && !out.back().rule) {
                    MdRow r;
                    r.rule = true;
                    out.push_back(std::move(r));
                }
                break;
            case MdBlock::Heading:
                wrap_para(out, text.substr(l.cl, l.ch - l.cl), MdTone::Heading,
                          w, max);
                break;
            case MdBlock::Quote: {  // one bar per nesting level
                std::string bars;
                for(int i = 0; i < std::max<int>(1, l.level); ++i)
                    bars += "\u258e ";
                const std::vector<MdSeg> pre{{bars, MdTone::Quote}};
                wrap_prefixed(out, text.substr(l.cl, l.ch - l.cl),
                              MdTone::Quote, w, max, pre, pre);
                break;
            }
            case MdBlock::Bullet: {  // hanging indent; nested items shift right
                const int lead = static_cast<int>(
                    std::min<size_t>(l.tl - l.lo, 8));
                std::string mark =
                    l.level ? std::string(text.substr(l.tl, l.me - l.tl))
                            : std::string("\u2022");
                mark += ' ';
                std::vector<MdSeg> first, rest;
                if(lead) {
                    first.push_back({std::string(lead, ' '), MdTone::Text});
                    rest.push_back({std::string(lead, ' '), MdTone::Text});
                }
                rest.push_back(
                    {std::string(static_cast<size_t>(text_width(mark)), ' '),
                     MdTone::Text});
                first.push_back({std::move(mark), MdTone::Bullet});
                wrap_prefixed(out, text.substr(l.cl, l.ch - l.cl),
                              MdTone::Text, w, max, first, rest);
                break;
            }
            case MdBlock::Text:
                wrap_para(out, text.substr(l.tl, l.th - l.tl), MdTone::Text, w,
                          max);
                break;
        }
    }
    while(!out.empty() && (out.back().rule || out.back().segs.empty()))
        out.pop_back();
    return out;
}
