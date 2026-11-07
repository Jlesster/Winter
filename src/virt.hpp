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
