#include "diag.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <vector>
#include "buffer.hpp"
#include "editor.hpp"
#include "options.hpp"
#include "piece.hpp"
#include "utf8.hpp"
#include "virt.hpp"

size_t lsp_offset(const PieceTable& pt, int64_t line, int64_t character,
                  bool utf16) {
    if(line < 0) return 0;
    if(static_cast<size_t>(line) >= pt.line_count()) return pt.size();
    const size_t ls = pt.line_start(static_cast<size_t>(line));
    const std::string text = pt.line_text(static_cast<size_t>(line));
    const size_t want = character < 0 ? 0 : static_cast<size_t>(character);
    if(!utf16) return ls + std::min(want, text.size());
    size_t units = 0, i = 0;
    while(i < text.size() && units < want) {
        const Decoded d = utf8_decode(text, i);
        units += d.cp >= 0x10000 ? 2 : 1;
        i += d.len;
    }
    return ls + i;
}

std::vector<Diag> diag_from_lsp(const PieceTable& pt, const Json& list,
                                bool utf16) {
    std::vector<Diag> out;
    if(!list.is_array()) return out;
    for(const Json& j : list.items()) {
        const Json& r = j["range"];
        Diag d;
        d.start = lsp_offset(pt,
                             r["start"]["line"].as_int(),
                             r["start"]["character"].as_int(),
                             utf16);
        d.end = lsp_offset(pt,
                           r["end"]["line"].as_int(),
                           r["end"]["character"].as_int(),
                           utf16);
        if(d.end < d.start) d.end = d.start;
        d.severity = static_cast<int>(
            std::clamp<int64_t>(j["severity"].as_int(1), 1, 4));
        d.msg = std::string(j["message"].as_str());
        d.source = std::string(j["source"].as_str());
        out.push_back(std::move(d));
    }
    std::ranges::sort(out, [](const Diag& a, const Diag& b) {
        return a.start != b.start ? a.start < b.start : a.severity < b.severity;
    });
    return out;
}

void diag_shift(std::vector<Diag>& v, const TextEdit& e) {
    for(Diag& d : v) {
        d.start = shift_pos(d.start, e, false);
        d.end = std::max(shift_pos(d.end, e, true), d.start);
    }
}

void diag_spans(const std::vector<Diag>& v, size_t ls, size_t len,
                std::vector<LineSpan>& out) {
    const size_t le = ls + len;
    for(const Diag& d : v) {
        if(d.start > le) break;
        if(d.severity > opts.diag_severity) continue;
        const size_t e = std::max(d.end, d.start + 1);
        if(e <= ls) continue;
        const size_t lo = std::max(d.start, ls) - ls, hi = std::min(e, le) - ls;
        if(hi > lo) {
            out.push_back({lo,
                           hi,
                           kPrioDiag - d.severity,
                           Style{.attrs = static_cast<uint16_t>(
                                     opts.diag_undercurl ? ATTR_UNDERCURL
                                                         : ATTR_UNDERLINE),
                                 .ul = diag_color(d.severity)}});
        }
    }
}

const Diag* diag_worst_on_line(const std::vector<Diag>& v, size_t ls,
                               size_t len) {
    auto it =
        std::lower_bound(v.begin(), v.end(), ls, [](const Diag& d, size_t x) {
            return d.start < x;
        });
    const Diag* best = nullptr;
    for(; it != v.end() && it->start <= ls + len; ++it)
        if(it->severity <= opts.diag_severity &&
           (!best || it->severity < best->severity))
            best = &*it;
    return best;
}

std::string diag_summary(const std::vector<Diag>& v) {
    int n[5] = {};
    for(const Diag& d : v)
        if(d.severity <= opts.diag_severity) ++n[d.severity];
    std::string s;
    if(n[1]) s += std::format("E{} ", n[1]);
    if(n[2]) s += std::format("W{} ", n[2]);
    if(n[3]) s += std::format("I{} ", n[3]);
    if(n[4]) s += std::format("H{} ", n[4]);
    return s;
}

uint32_t diag_color(int sev) {
    switch(sev) {
        case 1: return 0xf38ba8;
        case 2: return 0xf9e2af;
        case 3: return 0x89dceb;
        default: return 0x94e2d5;
    }
}

const char* diag_name(int sev) {
    static constexpr const char* n[] = {"", "error", "warning", "info", "hint"};
    return n[sev >= 1 && sev <= 4 ? sev : 1];
}

std::string_view diag_first_line(const Diag& d) {
    return std::string_view(d.msg).substr(0, d.msg.find('\n'));
}

void diag_jump(int dir, int count) {
    Buffer& b = E.buf();
    b.sync_anchors();
    std::vector<size_t> starts;
    for(const Diag& d : b.diags)
        if(d.severity <= opts.diag_severity &&
           (starts.empty() || starts.back() != d.start))
            starts.push_back(d.start);
    if(starts.empty()) {
        editor_set_status_message("No diagnostics");
        return;
    }
    size_t cur = cursor_offset();
    for(int k = 0; k < std::max(count, 1); ++k) {
        if(dir > 0) {
            const auto it = std::upper_bound(starts.begin(), starts.end(), cur);
            cur = it == starts.end() ? starts.front() : *it;
        } else {
            const auto it = std::lower_bound(starts.begin(), starts.end(), cur);
            cur = it == starts.begin() ? starts.back() : *(it - 1);
        }
    }
    set_cursor_offset(std::min(cur, b.pt.size()));
    for(const Diag& d : b.diags)
        if(d.start == cur && d.severity <= opts.diag_severity) {
            editor_set_status_message(
                "{}: {}", diag_name(d.severity), diag_first_line(d));
            break;
        }
}
