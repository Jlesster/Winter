#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "builtin.hpp"
#include "editor.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "piece.hpp"
#include "registry.hpp"
#include "utf8.hpp"

namespace {

struct Register {
    std::string text;
    bool linewise = false;
};
Register unnamed;

std::string indent_unit() {  // same rule as edit.cpp's private one
    return opts.expand_tab
               ? std::string(static_cast<size_t>(opts.shift_width), ' ')
               : std::string("\t");
}

std::optional<size_t> find_open(const PieceTable& t, size_t p, char o, char c,
                                bool self_open) {
    int depth = 0;
    for(size_t i = p + 1; i-- > 0;) {
        const char x = t.at(i);
        if(i == p) {
            if(x == o && self_open) return i;
            continue;
        }
        if(x == c) ++depth;
        else if(x == o) {
            if(depth == 0) return i;
            --depth;
        }
    }
    return std::nullopt;
}

std::optional<size_t> find_close(const PieceTable& t, size_t open_pos, char o,
                                 char c) {
    int depth = 0;
    for(size_t i = open_pos + 1; i < t.size(); ++i) {
        const char x = t.at(i);
        if(x == o) ++depth;
        else if(x == c) {
            if(depth == 0) return i;
            --depth;
        }
    }
    return std::nullopt;
}

template <char O, char C, bool Around>
std::optional<Range> bracket_obj(const PieceTable& t, size_t p, int count) {
    if(p >= t.size()) return std::nullopt;
    auto s = find_open(t, p, O, C, true);
    for(int n = std::max(count, 1); s && n > 1; --n)
        s = find_open(t, *s, O, C, false);
    if(!s) return std::nullopt;
    const auto e = find_close(t, *s, O, C);
    if(!e) return std::nullopt;
    if(Around) return Range{*s, *e + 1};
    size_t a = *s + 1;
    const size_t b = *e;
    const size_t close_ls = t.line_start(t.line_of(b));
    bool blank_before_close = true;
    for(size_t i = close_ls; i < b; ++i)
        if(t.at(i) != ' ' && t.at(i) != '\t') {
            blank_before_close = false;
            break;
        }
    if(a < b && t.at(a) == '\n' && blank_before_close) {
        ++a;
        if(a >= close_ls) return Range{a, a};
        return Range{a, close_ls, true};
    }
    return Range{a, b};
}

template <bool Big, bool Around>
std::optional<Range> word_obj(const PieceTable& t, size_t p, int count) {
    const size_t line = t.line_of(p);
    const size_t s = t.line_start(line), e = t.line_end(line);
    if(p >= e) return Range{p, p};
    auto klass = [&](size_t i) {
        const auto u = static_cast<unsigned char>(t.at(i));
        if(u == ' ' || u == '\t') return 0;
        if(Big || u >= 0x80 || std::isalnum(u) || u == '_') return 1;
        return 2;
    };
    auto run_end = [&](size_t i) {
        const int k = klass(i);
        while(i < e && klass(i) == k) ++i;
        return i;
    };
    size_t a = p;
    for(const int k = klass(p); a > s && klass(a - 1) == k;) --a;
    size_t b = a;
    bool lead = false;
    for(int n = std::max(count, 1); n > 0 && b < e; --n) {
        const bool on_blank = klass(b) == 0;
        b = run_end(b);
        if(!Around) continue;
        if(on_blank) {
            if(b < e) b = run_end(b);
        } else if(b < e && klass(b) == 0) {
            b = run_end(b);
        } else {
            lead = true;
        }
    }
    if(lead)
        while(a > s && klass(a - 1) == 0) --a;
    return Range{a, b};
}

// register text for a range; linewise text always ends in '\n'
std::string grab(const Range& r) {
    std::string s = E.buf().pt.text(r.start, r.end - r.start);
    if(r.linewise && (s.empty() || s.back() != '\n')) s += '\n';
    return s;
}

// removes the range, fills the register, returns where the cursor belongs.
// Placing the cursor is the caller's job: `d` clamps it, `c` must not.
size_t cut(const Range& r) {
    if(r.start == r.end && !r.linewise)
        return r.start;  // empty: keep the register
    unnamed = {grab(r), r.linewise};
    size_t a = r.start, n = r.end - r.start;
    // last line has no '\n' after it, so take the one before it instead
    if(r.linewise && (n == 0 || E.buf().pt.at(r.end - 1) != '\n') && a > 0) {
        --a;
        ++n;
    }
    E.buf().pt.remove(a, n);
    return a;
}

// Target (from a motion) -> Range (what the operator eats)
Range resolve(const PieceTable& t, size_t from, Target tg, bool word_motion) {
    size_t a = std::min(from, tg.pos), b = std::max(from, tg.pos);
    const size_t la = t.line_of(a), lb = t.line_of(b);

    if(tg.kind == MKind::Linewise)
        return {t.line_start(la), std::min(t.line_end(lb) + 1, t.size()), true};

    if(tg.kind == MKind::Inclusive) {
        if(b < t.line_end(lb)) {  // empty line: `$` selects nothing
            ++b;
            while(b < t.size() && utf8_is_cont(t.at(b))) ++b;
        }
        return {a, b, false};
    }

    // Exclusive
    if(word_motion && lb > la) {
        // dw on a line's last word: end at that word, don't join the next line
        while(b > a && std::isspace(static_cast<unsigned char>(t.at(b - 1))))
            --b;
    } else if(lb > la && b == t.line_start(lb)) {
        // vim's "o_v" rule: an exclusive motion ending at column 0 backs up to
        // the previous line's end; if it also started at/before the first
        // non-blank it becomes linewise (this is why d} deletes whole lines)
        b = t.line_end(lb - 1);
        if(a <= motion::to_first_nonblank(t, a, 0).pos)
            return {t.line_start(la), t.line_end(lb - 1) + 1, true};
    }
    return {a, b, false};
}

void shift(const Range& r, int dir) {  // > and <
    const size_t l0 = E.buf().pt.line_of(r.start);
    const size_t l1 = E.buf().pt.line_of(r.end > r.start ? r.end - 1 : r.start);
    const std::string unit = indent_unit();
    for(size_t l = l1 + 1;
        l-- > l0;) {  // bottom-up keeps earlier offsets valid
        const size_t s = E.buf().pt.line_start(l), e = E.buf().pt.line_end(l);
        if(dir > 0) {
            if(e > s) E.buf().pt.insert(s, unit);  // vim skips empty lines
        } else {
            size_t n = 0;
            if(s < e && E.buf().pt.at(s) == '\t') n = 1;
            else
                while(n < static_cast<size_t>(opts.shift_width) && s + n < e &&
                      E.buf().pt.at(s + n) == ' ')
                    ++n;
            if(n) E.buf().pt.remove(s, n);
        }
    }
    apply_motion(motion::to_first_nonblank(E.buf().pt, E.buf().pt.line_start(l0), 0));
}

void paste(bool after, int count) {  // p and P
    if(unnamed.text.empty()) return;
    PieceTable::Group g(E.buf().pt);
    std::string s;
    for(int i = std::max(count, 1); i > 0; --i) s += unnamed.text;

    if(unnamed.linewise) {
        size_t at;
        bool lead_nl = false;
        if(!after) {
            at = E.buf().pt.line_start(E.cy);
        } else if(const size_t e = E.buf().pt.line_end(E.cy); e == E.buf().pt.size()) {
            // last line has no '\n' to step over: move it to the paste's front
            at = e;
            lead_nl = true;
            s.pop_back();
            s.insert(0, "\n");
        } else {
            at = e + 1;
        }
        E.buf().pt.insert(at, s);
        apply_motion(
            motion::to_first_nonblank(E.buf().pt, at + (lead_nl ? 1 : 0), 0));
    } else {
        size_t at = cursor_offset();
        if(after)
            at = motion::right_op(E.buf().pt, at, 1)
                     .pos;  // 1 char right (or stay on empty line)
        E.buf().pt.insert(at, s);
        // multi-line: cursor at its start. single-line: on its last char.
        set_cursor_offset(s.find('\n') != std::string::npos
                              ? at
                              : motion::left(E.buf().pt, at + s.size(), 1).pos);
        update_want();
    }
}

struct Pair {
    std::string open, close;
};

std::optional<Pair> surround_pair(std::string_view k) {
    if(k.size() != 1) return std::nullopt;
    switch(k[0]) {
        case ')':
        case 'b': return Pair{"(", ")"};
        case '(': return Pair{"( ", " )"};
        case '}':
        case 'B': return Pair{"{", "}"};
        case '{': return Pair{"{ ", " }"};
        case ']':
        case 'r': return Pair{"[", "]"};
        case '[': return Pair{"[ ", " ]"};
        case '>':
        case 'a': return Pair{"<", ">"};
        case '<': return Pair{"< ", " >"};
    }
    const auto u = static_cast<unsigned char>(k[0]);
    if(std::isalnum(u) || u < 33 || u >= 127) return std::nullopt;
    return Pair{std::string(k), std::string(k)};
}

void surround(const Range& r, const Pair& p) {
    if(r.end <= r.start) return;
    PieceTable::Group g(E.buf().pt);
    if(!r.linewise) {
        E.buf().pt.insert(r.end, p.close);
        E.buf().pt.insert(r.start, p.open);
        set_cursor_offset(r.start);
        update_want();
        return;
    }
    const size_t l0 = E.buf().pt.line_of(r.start);
    const size_t l1 = E.buf().pt.line_of(r.end - 1);
    const std::string first = E.buf().pt.line_text(l0);
    const std::string indent =
        first.substr(0,
                     first.find_first_not_of(" \t") == std::string::npos
                         ? first.size()
                         : first.find_first_not_of(" \t"));
    const auto trim = [](std::string s) {
        const auto a = s.find_first_not_of(' ');
        const auto b = s.find_last_not_of(' ');
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    if(E.buf().pt.at(r.end - 1) == '\n')
        E.buf().pt.insert(r.end, indent + trim(p.close) + "\n");
    else E.buf().pt.insert(r.end, "\n" + indent + trim(p.close));
    const std::string unit = indent_unit();
    for(size_t l = l1 + 1; l-- > l0;)
        if(E.buf().pt.line_end(l) > E.buf().pt.line_start(l))
            E.buf().pt.insert(E.buf().pt.line_start(l), unit);
    E.buf().pt.insert(r.start, indent + trim(p.open) + "\n");
    apply_motion(motion::to_first_nonblank(E.buf().pt, r.start, 0));
}

// i" / a" etc: pairs on the line are 1-2, 3-4, ... like vim
std::optional<Range> quote(const PieceTable& t, size_t p, char q, bool around) {
    const size_t line = t.line_of(p), s = t.line_start(line),
                 e = t.line_end(line);
    std::vector<size_t> qs;
    for(size_t i = s; i < e; ++i)
        if(t.at(i) == q && (i == s || t.at(i - 1) != '\\')) qs.push_back(i);
    for(size_t k = 0; k + 1 < qs.size(); k += 2)
        if(p <= qs[k + 1])  // first pair that ends at/after the cursor
            return around ? Range{qs[k], qs[k + 1] + 1}
                          : Range{qs[k] + 1, qs[k + 1]};
    return std::nullopt;
}

template <char Q, bool Around>
std::optional<Range> quote_obj(const PieceTable& t, size_t p, int) {
    return quote(t, p, Q, Around);
}

// d, c, y, > <: operator key in NORMAL; doubled key (dd) = whole lines
void bind_op(const char* name, Operator fn, const char* keys) {
    registry().add_operator(name, std::move(fn));
    registry().add_action(name, [n = std::string(name)](Ctx& c) {
        normal_begin_operator(n, c.count);
    });
    registry().map(NORMAL, parse_keys(keys), name);
    registry().map(OPPEND, parse_keys(keys), "motion.line");
}

// x = dl, D = d$ ...: run an operator with a named motion
void bind_alias(const char* name, const char* op, const char* mot,
                const char* keys) {
    registry().add_action(name,
                          [=](Ctx& c) { run_operator(op, mot, c.count, {}); });
    registry().map(NORMAL, parse_keys(keys), name);
}

void bind_obj(const char* name, ObjectFn fn, const char* keys) {
    registry().add_object(name, fn);
    registry().map(OPPEND, parse_keys(keys), name);
    const std::string vname = std::string("visual.") + name;
    registry().add_action(vname, [fn](Ctx& c) {
        const auto r = fn(E.buf().pt, cursor_offset(), c.count);
        if(!r || r->end <= r->start) return;
        size_t last = r->end - 1;
        while(last > r->start && utf8_is_cont(E.buf().pt.at(last))) --last;
        E.v_anchor = r->start;
        E.v_line = false;
        set_cursor_offset(last);
        update_want();
    });
    registry().map(VISUAL, parse_keys(keys), vname);
}

void alias_obj(const char* keys, const char* name) {
    registry().map(OPPEND, parse_keys(keys), name);
    registry().map(VISUAL, parse_keys(keys), std::string("visual.") + name);
}

}  // namespace

Range visual_range() {
    const size_t cur = cursor_offset();
    const size_t a = std::min(E.v_anchor, cur), b = std::max(E.v_anchor, cur);
    if(E.v_line)
        return {E.buf().pt.line_start(E.buf().pt.line_of(a)),
                std::min(E.buf().pt.line_end(E.buf().pt.line_of(b)) + 1, E.buf().pt.size()),
                true};
    size_t end = b;
    if(end < E.buf().pt.size()) {
        ++end;
        while(end < E.buf().pt.size() && utf8_is_cont(E.buf().pt.at(end))) ++end;
    }
    return {a, end, false};
}

void run_operator_on(std::string_view op, const Range& r) {
    const Operator* fn = registry().op(op);
    if(!fn) return;
    PieceTable::Group g(E.buf().pt);
    (*fn)(r);
}

void run_operator(std::string_view op, std::string_view what, int count,
                  std::string_view arg) {
    const Operator* fn = registry().op(op);
    if(!fn) return;
    const size_t cur = cursor_offset();
    std::optional<Range> r;

    if(const ObjectFn obj = registry().object(what)) {
        r = obj(E.buf().pt, cur, count);
    } else if(const motion::Fn mot = registry().motion(what)) {
        const bool word =
            what == "motion.word_next" || what == "motion.WORD_next";
        motion::Fn use = mot;
        // cw acts like ce when on a non-blank (vim quirk)
        if(op == "op.change" && word && cur < E.buf().pt.size() &&
           !std::isspace(static_cast<unsigned char>(E.buf().pt.at(cur))))
            use = registry().motion(what == "motion.word_next"
                                        ? "motion.word_end"
                                        : "motion.WORD_end");
        const Target t = use(E.buf().pt, cur, count, arg);
        if(!t.ok) return;
        r = resolve(E.buf().pt, cur, t, word);
    }
    if(!r) return;

    PieceTable::Group g(E.buf().pt);  // the whole operator is one undo step
    (*fn)(*r);
}

void register_builtin_operators() {
    bind_op(
        "op.delete",
        [](const Range& r) {
            set_cursor_offset(std::min(cut(r), E.buf().pt.size()));
            apply_motion(
                r.linewise
                    ? motion::to_first_nonblank(E.buf().pt, cursor_offset(), 0)
                    : Target{motion::clamp_normal(E.buf().pt, cursor_offset())});
        },
        "d");

    bind_op(
        "op.yank",
        [](const Range& r) {
            unnamed = {grab(r), r.linewise};
            if(!r.linewise)
                apply_motion(Target{r.start});  // yy leaves the cursor alone
        },
        "y");

    bind_op(
        "op.change",
        [](const Range& r) {
            if(r.linewise) {  // cc: wipe the text, keep the line and its indent
                const std::string line = E.buf().pt.line_text(E.buf().pt.line_of(r.start));
                const size_t w = line.find_first_not_of(" \t");
                const std::string indent =
                    line.substr(0, w == std::string::npos ? line.size() : w);
                unnamed = {grab(r), true};
                size_t b = r.end;
                if(b > r.start && E.buf().pt.at(b - 1) == '\n') --b;  // keep the '\n'
                E.buf().pt.remove(r.start, b - r.start);
                E.buf().pt.insert(r.start, indent);
                set_cursor_offset(r.start + indent.size());
            } else {
                set_cursor_offset(
                    cut(r));  // unclamped: insert may sit past the last char
            }
            set_mode(INSERT);
        },
        "c");

    bind_op("op.indent", [](const Range& r) { shift(r, +1); }, ">");
    bind_op("op.dedent", [](const Range& r) { shift(r, -1); }, "<");

    bind_alias("edit.x", "op.delete", "motion.right", "x");
    bind_alias("edit.X", "op.delete", "motion.left", "X");
    bind_alias("edit.D", "op.delete", "motion.line_end", "D");
    bind_alias("edit.C", "op.change", "motion.line_end", "C");
    bind_alias("edit.s", "op.change", "motion.right", "s");
    bind_alias("edit.S", "op.change", "motion.line", "S");
    bind_alias("edit.Y", "op.yank", "motion.line", "Y");

    registry().add_action("edit.paste_after",
                          [](Ctx& c) { paste(true, c.count); });
    registry().add_action("edit.paste_before",
                          [](Ctx& c) { paste(false, c.count); });
    registry().map(NORMAL, "p", "edit.paste_after");
    registry().map(NORMAL, "P", "edit.paste_before");

    bind_obj("obj.i\"", quote_obj<'"', false>, "i\"");
    bind_obj("obj.a\"", quote_obj<'"', true>, "a\"");
    bind_obj("obj.i'", quote_obj<'\'', false>, "i'");
    bind_obj("obj.a'", quote_obj<'\'', true>, "a'");
    bind_obj("obj.i`", quote_obj<'`', false>, "i`");
    bind_obj("obj.a`", quote_obj<'`', true>, "a`");

    bind_obj("obj.i(", bracket_obj<'(', ')', false>, "i(");
    bind_obj("obj.a(", bracket_obj<'(', ')', true>, "a(");
    bind_obj("obj.i{", bracket_obj<'{', '}', false>, "i{");
    bind_obj("obj.a{", bracket_obj<'{', '}', true>, "a{");
    bind_obj("obj.i[", bracket_obj<'[', ']', false>, "i[");
    bind_obj("obj.a[", bracket_obj<'[', ']', true>, "a[");
    bind_obj("obj.i<", bracket_obj<'<', '>', false>, "i<");
    bind_obj("obj.a<", bracket_obj<'<', '>', true>, "a<");
    alias_obj("i)", "obj.i(");
    alias_obj("ib", "obj.i(");
    alias_obj("a)", "obj.a(");
    alias_obj("ab", "obj.a(");
    alias_obj("i}", "obj.i{");
    alias_obj("iB", "obj.i{");
    alias_obj("a}", "obj.a{");
    alias_obj("aB", "obj.a{");
    alias_obj("i]", "obj.i[");
    alias_obj("a]", "obj.a[");

    bind_obj("obj.iw", word_obj<false, false>, "iw");
    bind_obj("obj.aw", word_obj<false, true>, "aw");
    bind_obj("obj.iW", word_obj<true, false>, "iW");
    bind_obj("obj.aW", word_obj<true, true>, "aW");

    // visual mode: the selection *is* the range, so run the operator now.
    // visual_range() must be read BEFORE leaving visual (it needs the anchor).
    auto bind_visual_op =
        [](const char* name, const char* op, const char* keys) {
            registry().add_action(name, [op](Ctx&) {
                const Range r = visual_range();
                set_mode(NORMAL);
                run_operator_on(op, r);
            });
            registry().map(VISUAL, parse_keys(keys), name);
        };
    bind_visual_op("visual.delete", "op.delete", "d");
    bind_visual_op("visual.delete_x", "op.delete", "x");
    bind_visual_op("visual.change", "op.change", "c");
    bind_visual_op("visual.change_s", "op.change", "s");
    bind_visual_op("visual.yank", "op.yank", "y");
    bind_visual_op("visual.indent", "op.indent", ">");
    bind_visual_op("visual.dedent", "op.dedent", "<");

    registry().add_action(
        "visual.surround",
        [](Ctx& c) {
            const auto p = surround_pair(c.arg);
            if(!p) return;
            const Range r = visual_range();
            set_mode(NORMAL);
            surround(r, *p);
        },
        /*wants_char=*/true);
    registry().map(VISUAL, "S", "visual.surround");
}
