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

#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "buffer.hpp"
#include "piece.hpp"

struct VirtText {
    std::string text;
    Style style;
};

using NsId = uint32_t;
NsId deco_ns(std::string_view name);

struct EolDeco {
    size_t pos = 0;
    NsId ns = 0;
    int prio = 0;
    VirtText vt;
};

constexpr int kPrioDiag = 100;

struct LineSpan {
    size_t lo, hi;
    int prio;
    Style st;
};
void style_patch(Style& base, const Style& p);
void compose_spans(std::vector<LineSpan>& in, std::vector<LineSpan>& out);

struct SpanDeco {
    size_t start = 0, end = 0;
    NsId ns = 0;
    int prio = 0;
    Style st;
    bool fill = false;  // also tints the empty tail of every row it touches
    // An edit that touches it cuts the touched part out instead of stretching
    // it: used for async data (semantic tokens) that describes the old text.
    bool strict = false;
};

struct EolItem {
    int prio;
    const VirtText* vt;
    int gap = 2;  // columns after this item, before the next
};

class DecoStore {
public:
    void apply(const TextEdit& e);
    void add(EolDeco d);
    void add_span(SpanDeco d);
    void clear_ns(NsId ns);
    // Drop every span/eol of `ns` and put `spans` in their place in one pass
    // (add_span per span is an insert each). Spans need not be sorted.
    void replace_ns_spans(NsId ns, std::vector<SpanDeco> spans);
    bool empty() const { return _eol.empty() && _spans.empty(); }
    void eol_on_line(size_t ls, size_t len, std::vector<EolItem>& out) const;
    void spans_on_line(size_t ls, size_t len, std::vector<LineSpan>& out) const;
    // composed style of the `fill` spans touching a line, if any
    std::optional<Style> fill_on_line(size_t ls, size_t len) const;

private:
    std::vector<EolDeco> _eol;
    std::vector<SpanDeco> _spans;
    size_t _max_len = 0;
};
