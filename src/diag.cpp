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

#include "diag.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <set>
#include <vector>
#include "buffer.hpp"
#include "editor.hpp"
#include "jumplist.hpp"
#include "lsp_sync.hpp"
#include "options.hpp"
#include "picker.hpp"
#include "piece.hpp"
#include "theme.hpp"
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

size_t lsp_col(const PieceTable& pt, size_t offset, bool utf16) {
    const size_t ls = pt.line_start(pt.line_of(offset));
    const std::string text = pt.text(ls, offset - ls);
    if(!utf16) return text.size();
    size_t units = 0;
    for(size_t i = 0; i < text.size();) {
        const Decoded d = utf8_decode(text, i);
        units += d.cp >= 0x10000 ? 2 : 1;
        i += d.len;
    }
    return units;
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
        d.raw = j;
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
            Style us = theme_ui(
                "diag.underline",
                Style{.attrs = static_cast<uint16_t>(opts.diag_undercurl
                                                         ? ATTR_UNDERCURL
                                                         : ATTR_UNDERLINE)});
            if(us.ul == 0xFFFFFFFF) us.ul = diag_color(d.severity);
            out.push_back({lo, hi, kPrioDiag - d.severity, us});
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
        case 1: return theme_fg("diag.error", 0xf38ba8);
        case 2: return theme_fg("diag.warn", 0xf9e2af);
        case 3: return theme_fg("diag.info", 0x89dceb);
        default: return theme_fg("diag.hint", 0x94e2d5);
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

namespace {
namespace fs = std::filesystem;

struct DiagRow {
    std::string path;
    size_t line = 0;       // 0-based
    size_t col = 0;        // live: byte column. published: server character
    bool live = true;      // from an open buffer (col is bytes, else see utf16)
    bool utf16 = false;
    int severity = 1;
    std::string msg, source;
};

std::string diag_abs_path(const Buffer& b) {
    std::error_code ec;
    return fs::absolute(b.filename, ec).lexically_normal().string();
}

void diag_rows_of(Buffer& b, std::vector<DiagRow>& out) {
    if(b.nofile || b.filename.empty()) return;
    b.sync_anchors();  // offsets below are for the current text
    const std::string path = diag_abs_path(b);
    for(const Diag& d : b.diags) {
        if(d.severity > opts.diag_severity) continue;
        const LineCol lc = b.pt.to_linecol(std::min(d.start, b.pt.size()));
        out.push_back({path, lc.line, lc.col, true, false, d.severity,
                       std::string(diag_first_line(d)), d.source});
    }
}
}  // namespace

void diag_picker(bool workspace) {
    std::vector<DiagRow> rows;
    if(!workspace) {
        diag_rows_of(E.buf(), rows);
    } else {
        std::set<std::string> open;
        for(auto& up : E.buffers) {
            if(up->nofile || up->filename.empty()) continue;
            open.insert(diag_abs_path(*up));
            diag_rows_of(*up, rows);
        }
        for(const PublishedDiag& p : lsp_sync_published()) {
            if(p.severity > opts.diag_severity || open.contains(p.path))
                continue;
            const std::string_view m(p.msg);
            rows.push_back({p.path, static_cast<size_t>(p.line),
                            static_cast<size_t>(p.character), false, p.utf16,
                            p.severity, std::string(m.substr(0, m.find('\n'))),
                            p.source});
        }
    }
    if(rows.empty()) {
        editor_set_status_message("No diagnostics");
        return;
    }
    // buffer: reading order. workspace: worst first, then by file.
    std::ranges::sort(rows, [&](const DiagRow& a, const DiagRow& b) {
        if(workspace && a.severity != b.severity) return a.severity < b.severity;
        if(workspace && a.path != b.path) return a.path < b.path;
        return a.line != b.line ? a.line < b.line
               : a.col != b.col ? a.col < b.col
                                : a.severity < b.severity;
    });

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    std::vector<PickItem> items;
    items.reserve(rows.size());
    for(size_t i = 0; i < rows.size(); ++i) {
        const DiagRow& r = rows[i];
        std::string where = std::format("{}:{}", r.line + 1, r.col + 1);
        if(workspace) {
            const fs::path rel = fs::path(r.path).lexically_relative(cwd);
            const bool inside = !rel.empty() && !rel.native().starts_with("..");
            where = std::format("{}:{}", inside ? rel.string() : r.path,
                                where);
        }
        PickItem it;
        it.id = i;
        it.path = r.path;
        it.line = static_cast<int>(r.line) + 1;
        it.col = r.live ? static_cast<int>(r.col) + 1 : 1;
        it.text = std::format("{:<8}{}  {}{}", diag_name(r.severity), where,
                              r.msg,
                              r.source.empty() ? "" : "  [" + r.source + "]");
        items.push_back(std::move(it));
    }

    auto keep = std::make_shared<std::vector<DiagRow>>(std::move(rows));
    PickerSpec s;
    s.prompt = workspace ? "Workspace diagnostics> " : "Diagnostics> ";
    s.fuzzy = true;
    s.preview = true;
    s.on_accept = [keep](const PickItem& it) {
        if(it.id >= keep->size()) return;
        const DiagRow& r = (*keep)[it.id];
        jump_push();
        if(!jump_goto(r.path, r.line, r.live ? r.col : 0)) {
            editor_set_status_message("diagnostics: cannot open {}", r.path);
            return;
        }
        if(r.live) return;
        // published position: character units -> bytes, now the file is open
        Buffer& b = E.buf();
        set_cursor_offset(std::min(
            lsp_offset(b.pt, static_cast<int64_t>(r.line),
                       static_cast<int64_t>(r.col), r.utf16),
            b.pt.size()));
    };
    picker_open(std::move(s));
    picker_add(std::move(items));
}
