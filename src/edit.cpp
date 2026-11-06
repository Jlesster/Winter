#include "edit.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "indent.hpp"
#include "keys.hpp"
#include "options.hpp"
#include "piece.hpp"
#include "utf8.hpp"

namespace {
std::vector<std::string> g_replaced;
std::string g_pending;
size_t g_ai = SIZE_MAX;
}  // namespace

// edits
void editor_insert_char(int c) {
    g_ai = SIZE_MAX;
    const char ch = static_cast<char>(c);
    E.buf().pt.insert(cursor_offset(), std::string_view(&ch, 1));
    E.cx++;
}

void editor_insert_tab() {
    g_ai = SIZE_MAX;
    if(!opt_et()) return editor_insert_char('\t');
    // pad to the next shift-width column, not a fixed 4 spaces
    const int rx = cx_to_rx(E.buf().pt.line_text(E.cy), E.cx);
    const int n = opt_sw() - (rx % opt_sw());
    E.buf().pt.insert(cursor_offset(),
                      std::string(static_cast<size_t>(n), ' '));
    E.cx += n;
}

namespace {

// auto pairs

static char closer_for(char c) {
    switch(c) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        default: return 0;
    }
}

static bool is_closer(char c) { return c == ')' || c == ']' || c == '}'; }

static bool is_quote(char c) { return c == '"' || c == '\'' || c == '`'; }

static bool is_word(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

using Lit = Syntax::Lit;

constexpr std::string_view kBacktick[] = {
    "javascript", "typescript", "go", "markdown"};

static char opener_for(char c) {
    switch(c) {
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default: return 0;
    }
}

// Is the cursor inside a string/comment? Probes the byte before AND the byte
// after the cursor, so it doesn't depend on exactly where a grammar puts its
// span boundaries (python's string_start/string_end tokens, c's string_content,
// ...). Both bytes must be literal for the cursor to be "inside".
// Comment at end of line is also inside (line comments don't include the \n).
static Lit lit_kind(Buffer& B, size_t off, char next) {
    B.syntax.update(B.pt, B.eol);  // same call indent::from_tree makes
    if(off == 0) return Lit::None;
    const Syntax::LitSpan a = B.syntax.literal_at(off - 1);
    if(a.kind == Lit::None) return Lit::None;
    if(B.syntax.literal_at(off).kind == a.kind) return a.kind;
    return (a.kind == Lit::Comment && next == 0) ? Lit::Comment : Lit::None;
}

// Unescaped `q` count in `before` is odd => we're inside a string of that
// quote kind. Only looks left of the cursor (the old code counted the line).
static bool odd_quotes_before(std::string_view before, char q) {
    int n = 0;
    for(size_t i = 0; i < before.size(); ++i) {
        if(before[i] == '\\') ++i;  // skip the escaped char
        else if(before[i] == q) ++n;
    }
    return n % 2 != 0;
}

// How many `open`s left of the cursor are still unclosed? Scans back up to 64
// lines, counting only brackets in the same kind of context as the cursor.
static int unclosed_before(Buffer& B, char open, char close, Lit here) {
    const size_t cy = static_cast<size_t>(E.cy);
    int depth = 0, unclosed = 0;
    for(size_t l = cy + 1, n = 0; l-- > 0 && n < 64; ++n) {
        const std::string line = B.pt.line_text(l);
        const size_t base = B.pt.line_start(l);
        const size_t end = l == cy ? static_cast<size_t>(E.cx) : line.size();
        for(size_t i = end; i-- > 0;) {
            const char c = line[i];
            if(c != open && c != close) continue;
            if(B.syntax.literal_at(base + i).kind != here) continue;
            if(c == close) ++depth;
            else if(depth > 0) --depth;
            else ++unclosed;
        }
    }
    return unclosed;
}

// How many `close`s in `rest` (text right of the cursor) have no `open` in
// `rest` to pair with, i.e. they belong to something left of the cursor.
static int unmatched_after(std::string_view rest, char open, char close) {
    int depth = 0, n = 0;
    for(const char c : rest) {
        if(c == open) ++depth;
        else if(c == close) {
            if(depth > 0) --depth;
            else ++n;
        }
    }
    return n;
}

// python string prefixes (r f b u rb br fr rf, any case) and C/C++ ones
// (R L u U u8 + R combos). Only matters for ', since " always pairs.
static bool is_str_prefix(std::string_view lang, std::string_view w) {
    if(lang == "python") {
        std::string s(w);
        for(char& ch : s)
            ch =
                static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        constexpr std::string_view k[] = {
            "r", "f", "b", "u", "rb", "br", "fr", "rf"};
        return std::ranges::find(k, std::string_view(s)) != std::end(k);
    }
    if(lang == "c" || lang == "cpp") {
        constexpr std::string_view k[] = {
            "R", "L", "u", "U", "u8", "LR", "uR", "UR", "u8R"};
        return std::ranges::find(k, w) != std::end(k);
    }
    return false;
}

// Per-language / per-context rules for whether a quote is allowed to pair.
static bool can_pair_quote(char q, std::string_view before,
                           std::string_view lang) {
    if(q == '`')
        return lang.empty() ||
               std::ranges::find(kBacktick, lang) != std::end(kBacktick);
    if(q == '"') return true;         // f"  R"  u8"  all fine
    if(lang == "rust") return false;  // &'a, <'a>
    size_t i = before.size();         // the word right before the cursor
    while(i > 0 && is_word(before[i - 1])) --i;
    const std::string_view word = before.substr(i);
    // don't, 1'000 -> no pair.  f'  b'  u8'  L' -> pair.
    return word.empty() || is_str_prefix(lang, word);
}

// Backspace: `prev` is the char left of the cursor, `next` the one right of
// it, `before` is the text left of `prev`. True only if prev+next is an empty
// pair that prev opened.
static bool is_empty_pair(char prev, char next, std::string_view before) {
    if(!next) return false;
    if(closer_for(prev)) return closer_for(prev) == next;
    if(!is_quote(prev) || prev != next) return false;
    if(!before.empty() && before.back() == '\\') return false;  // \"|"
    // even count to the left => prev opened a string. Odd => prev CLOSED one,
    // so `"a"|"b"` no longer eats both quotes.
    return !odd_quotes_before(before, prev);
}

constexpr std::string_view kElectric[] = {"else",
                                          "elif",
                                          "elseif",
                                          "end",
                                          "until",
                                          "case",
                                          "default",
                                          "catch",
                                          "finally",
                                          "except",
                                          "rescue",
                                          "ensure",
                                          "fi",
                                          "done",
                                          "esac"};

void set_line_indent(const std::string& ws) {
    Buffer& B = E.buf();
    const std::string line = B.pt.line_text(E.cy);
    const std::string_view cur = indent::leading_ws(line);
    if(cur == ws) return;
    const size_t at = B.pt.line_start(E.cy);
    if(!cur.empty()) B.pt.remove(at, cur.size());
    if(!ws.empty()) B.pt.insert(at, ws);
    E.cx += static_cast<int>(ws.size()) - static_cast<int>(cur.size());
}

std::string one_level_less(std::string_view lw) {
    std::string s(lw);
    if(!s.empty() && s.back() == '\t') s.pop_back();
    else
        for(int i = 0; i < opt_sw() && !s.empty() && s.back() == ' ';
            ++i)
            s.pop_back();
    return s;
}

bool electric_word(std::string_view w) {
    return std::ranges::find(kElectric, w) != std::end(kElectric);
}

void electric_indent() {
    Buffer& B = E.buf();
    const std::string line = B.pt.line_text(E.cy);
    const std::string_view head = std::string_view(line).substr(0, E.cx);
    const std::string_view tok = head.substr(indent::leading_ws(head).size());
    const bool closer = tok.size() == 1 && is_closer(tok[0]);
    const bool lead = closer || tok == "{" || tok == "," || tok == "#";
    // an electric word fires when it completes, and once more on the next
    // keystroke, so `else` then `where` doesn't leave `elsewhere` dedented
    const bool word =
        electric_word(tok) ||
        (!tok.empty() && electric_word(tok.substr(0, tok.size() - 1)));
    if(!lead && !word && !indent::is_label(tok)) return;
    if(const auto t = indent::from_tree(B.indent, B.syntax, B.pt, B.eol, E.cy))
        set_line_indent(*t);
    else if(closer) set_line_indent(one_level_less(indent::leading_ws(head)));
}

}  // namespace

void editor_strip_autoindent() {
    Buffer& B = E.buf();
    const size_t l = std::exchange(g_ai, SIZE_MAX);
    if(l == SIZE_MAX || l >= B.pt.line_count()) return;
    const std::string t = B.pt.line_text(l);
    if(t.empty() || t.find_first_not_of(" \t") != std::string::npos) return;
    B.pt.remove(B.pt.line_start(l), t.size());
    if(static_cast<size_t>(E.cy) == l) E.cx = 0;
}

void editor_insert_newline() {
    Buffer& B = E.buf();
    const std::string line = B.pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const std::string_view before = std::string_view(line).substr(0, cx);
    const std::string_view after = std::string_view(line).substr(cx);

    const std::string base(indent::leading_ws(before));
    const size_t last = before.find_last_not_of(" \t");
    const char prev = last == std::string_view::npos ? 0 : before[last];

    // cursor sat in pure whitespace: don't leave trailing whitespace behind
    if(last == std::string_view::npos && cx > 0) {
        B.pt.remove(cursor_offset() - cx, cx);
        E.cx = 0;
    }

    // whitespace right after cursor is replaced by indent
    if(const size_t lw = indent::leading_ws(after).size())
        B.pt.remove(cursor_offset(), lw);

    const char cl = closer_for(prev);
    const bool split =
        cl && after.substr(indent::leading_ws(after).size()).starts_with(cl);
    B.pt.insert(cursor_offset(), split ? "\n\n" : "\n");
    E.cy++;
    auto ws = [&](size_t l, const std::string& fallback) {
        const auto t = indent::from_tree(B.indent, B.syntax, B.pt, B.eol, l);
        return t ? *t : fallback;
    };
    const std::string mid = ws(E.cy, indent::for_newline(before));
    const std::string close = split ? ws(E.cy + 1, base) : std::string{};
    if(split) B.pt.insert(B.pt.line_start(E.cy + 1), close);
    B.pt.insert(B.pt.line_start(E.cy), mid);
    g_ai = mid.empty() ? SIZE_MAX : static_cast<size_t>(E.cy);
    E.cx = static_cast<int>(mid.size());
}

// typing a printable ascii char in insert mode
void editor_type_char(int c) {
    g_ai = SIZE_MAX;
    const char ch = static_cast<char>(c);
    Buffer& B = E.buf();
    const std::string line = B.pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const std::string_view before = std::string_view(line).substr(0, cx);
    const std::string_view after = std::string_view(line).substr(cx);
    const char prev = cx > 0 ? line[cx - 1] : 0;
    const char next = cx < line.size() ? line[cx] : 0;
    const Lit here = lit_kind(B, cursor_offset(), next);
    if(next == ch) {
        const bool skip =
            is_closer(ch)  ? unclosed_before(B, opener_for(ch), ch, here)
            : is_quote(ch) ? prev != '\\' && odd_quotes_before(before, ch)
                           : false;
        if(skip) {
            E.cx++;
            return;
        }
    }

    // electric dedent: a closer on a whitespace-only line pulls back one level
    if(is_closer(ch)) {
        editor_insert_char(c);
        electric_indent();
        return;
    }

    // only pair when what follows can't be part of the thing we're opening;
    // another opener counts as room, so foo(|[ pairs
    const bool room = next == 0 || next == ' ' || next == '\t' || next == ',' ||
                      next == ';' || is_closer(next) || closer_for(next) != 0;
    if(const char cl = closer_for(ch)) {
        // never in strings; fine in code and comments. The spare-closer check
        // now only looks right of the cursor.
        const bool pair =
            room && here != Lit::String &&
            unmatched_after(after, ch, cl) <= unclosed_before(B, ch, cl, here);
        E.buf().pt.insert(cursor_offset(),
                          pair ? std::string{ch, cl} : std::string{ch});
        E.cx++;
        if(ch == '{') electric_indent();
        return;
    }
    if(is_quote(ch)) {
        // never in strings or comments, never after a backslash, never when
        // an odd quote to the left means this one closes something
        const bool pair = room && here == Lit::None && prev != ch &&
                          prev != '\\' && !odd_quotes_before(before, ch) &&
                          can_pair_quote(ch, before, B.syntax.lang());
        if(pair) {
            E.buf().pt.insert(cursor_offset(), std::string{ch, ch});
            E.cx++;
        } else editor_insert_char(c);
        return;
    }
    editor_insert_char(c);
    if(is_word(ch) || ch == ':' || ch == '#' || ch == ',') electric_indent();
}

void editor_open_below() {
    Buffer& B = E.buf();
    const std::string heur = indent::for_newline(B.pt.line_text(E.cy));
    B.pt.insert(B.pt.line_end(E.cy), "\n");
    E.cy++;
    const auto t = indent::from_tree(B.indent, B.syntax, B.pt, B.eol, E.cy);
    const std::string ws = t ? *t : heur;
    B.pt.insert(B.pt.line_start(E.cy), ws);
    g_ai = ws.empty() ? SIZE_MAX : static_cast<size_t>(E.cy);
    E.cx = static_cast<int>(ws.size());
}

void editor_open_above() {
    Buffer& B = E.buf();
    const std::string heur = indent::for_open_above(B.pt.line_text(E.cy));
    B.pt.insert(E.cy > 0 ? B.pt.line_end(E.cy - 1) : 0, "\n");
    const auto t = indent::from_tree(B.indent, B.syntax, B.pt, B.eol, E.cy);
    const std::string ws = t ? *t : heur;
    B.pt.insert(B.pt.line_start(E.cy), ws);
    E.cx = static_cast<int>(ws.size());
}

void editor_delete_char() {
    const size_t pos = cursor_offset();
    if(pos == 0) return;
    if(E.cx > 0) {
        const std::string line = E.buf().pt.line_text(E.cy);
        const char next = E.cx < static_cast<int>(line.size()) ? line[E.cx] : 0;
        if(is_empty_pair(
               line[E.cx - 1],
               next,
               std::string_view(line).substr(0, E.cx - 1))) {  // (|) -> |
            E.buf().pt.remove(pos - 1, 2);
            E.cx--;
            return;
        }
        const int from = static_cast<int>(utf8_prev(line, E.cx));
        E.buf().pt.remove(pos - (E.cx - from), E.cx - from);
        E.cx = from;
    } else {
        const int prev_len = static_cast<int>(line_len(E.cy - 1));
        E.buf().pt.remove(pos - 1, 1);
        E.cy--;
        E.cx = prev_len;
    }
}

size_t word_back_stop(std::string_view line, size_t pos) {
    const auto cls = [&](size_t k) {
        const auto c = static_cast<unsigned char>(line[k - 1]);
        if(c == ' ' || c == '\t') return 0;
        return (c >= 0x80 || std::isalnum(c) || c == '_') ? 1 : 2;
    };
    size_t i = pos;
    while(i > 0 && cls(i) == 0) --i;
    if(i == 0) return 0;
    const int c = cls(i);
    while(i > 0 && cls(i) == c) --i;
    return i;
}

void editor_delete_word_back() {
    const size_t pos = cursor_offset();
    if(pos == 0) return;
    if(E.cx == 0) return editor_delete_char();
    const size_t i =
        word_back_stop(E.buf().pt.line_text(E.cy), static_cast<size_t>(E.cx));
    const size_t n = static_cast<size_t>(E.cx) - i;
    E.buf().pt.remove(pos - n, n);
    E.cx = i;
}

void replace_reset() {
    g_replaced.clear();
    g_pending.clear();
}

void replace_char_at_cursor(int count, std::string_view ch) {
    PieceTable& pt = E.buf().pt;
    const std::string line = pt.line_text(E.cy);
    const size_t n = static_cast<size_t>(std::max(count, 1));
    const size_t start = static_cast<size_t>(E.cx);
    size_t end = start;
    for(size_t i = 0; i < n; ++i) {
        if(end >= line.size()) return;
        end = utf8_next(line, end);
    }
    std::string rep;
    for(size_t i = 0; i < n; ++i) rep += ch;
    const size_t at = cursor_offset();
    PieceTable::Group g(pt);
    pt.remove(at, end - start);
    pt.insert(at, rep);
    E.cx = static_cast<int>(start + rep.size() - ch.size());
    E.want_rx = cx_to_rx(pt.line_text(E.cy), E.cx);
}

bool replace_key(int c) {
    PieceTable& pt = E.buf().pt;
    if(c == BACKSPACE || c == CTRL_KEY('h')) {
        g_pending.clear();
        const std::string line = pt.line_text(E.cy);
        const size_t cx = static_cast<size_t>(E.cx);
        const size_t prev = utf8_prev(line, cx);
        if(g_replaced.empty()) {
            E.cx = static_cast<int>(prev);
            return true;
        }
        const std::string old = std::move(g_replaced.back());
        g_replaced.pop_back();
        E.cx = static_cast<int>(prev);
        pt.remove(cursor_offset(), cx - prev);
        if(!old.empty()) pt.insert(cursor_offset(), old);
        return true;
    }
    std::string ch;
    if(c >= 32 && c < 127) {
        ch.assign(1, static_cast<char>(c));
    } else if(c >= 0x80 && c < 0x100) {
        g_pending += static_cast<char>(c);
        if(g_pending.size() < utf8_seq_len(g_pending[0])) return true;
        ch = std::move(g_pending);
        g_pending.clear();
    } else {
        replace_reset();
        return false;
    }
    const std::string line = pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    std::string old;
    if(cx < line.size()) {
        const size_t n = utf8_next(line, cx) - cx;
        old = line.substr(cx, n);
        pt.remove(cursor_offset(), n);
    }
    g_replaced.push_back(std::move(old));
    pt.insert(cursor_offset(), ch);
    E.cx += static_cast<int>(ch.size());
    return true;
}

void editor_paste(std::string text) {
    std::string s;
    s.reserve(text.size());
    for(size_t i = 0; i < text.size(); ++i) {
        if(text[i] != '\r') {
            s += text[i];
            continue;
        }
        s += '\n';
        if(i + 1 < text.size() && text[i + 1] == '\n') ++i;
    }
    if(s.empty()) return;
    if(E.mode == VISUAL) set_mode(NORMAL);
    const bool ins = insertish(E.mode);
    PieceTable::Group g(E.buf().pt);
    size_t at = cursor_offset();
    if(!ins) {
        const std::string line = E.buf().pt.line_text(E.cy);
        if(E.cx < static_cast<int>(line.size()))
            at += utf8_next(line, static_cast<size_t>(E.cx)) -
                  static_cast<size_t>(E.cx);
    }
    E.buf().pt.insert(at, s);
    PieceTable& pt = E.buf().pt;
    const size_t end = at + s.size();
    const size_t l0 = pt.line_of(at), l1 = pt.line_of(end);
    const size_t tail = pt.line_end(l1) - end;
    if(opts.paste_indent && l1 > l0) {
        const size_t from = (at == pt.line_start(l0)) ? l0 : l0 + 1;
        indent::reindent_lines(
            E.buf().indent, E.buf().syntax, pt, E.buf().eol, from, l1);
    }
    set_cursor_offset(pt.line_end(l1) - tail);
    const std::string line = E.buf().pt.line_text(E.cy);
    if(!ins && E.cx > 0)
        E.cx = static_cast<int>(utf8_prev(line, static_cast<size_t>(E.cx)));
    E.want_rx = cx_to_rx(line, E.cx);
}

void editor_delete_forward() {
    const std::string line = E.buf().pt.line_text(E.cy);
    const size_t pos = cursor_offset();
    if(E.cx < static_cast<int>(line.size())) {
        const int to = static_cast<int>(utf8_next(line, E.cx));
        E.buf().pt.remove(pos, static_cast<size_t>(to - E.cx));
    } else if(E.cy < E.num_rows() - 1) {
        E.buf().pt.remove(pos, 1);
    }
}
