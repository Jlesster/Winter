#include "edit.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "indent.hpp"
#include "options.hpp"
#include "piece.hpp"
#include "utf8.hpp"

// edits

void editor_insert_char(int c) {
    const char ch = static_cast<char>(c);
    E.buf().pt.insert(cursor_offset(), std::string_view(&ch, 1));
    E.cx++;
}

void editor_insert_tab() {
    if(!opts.expand_tab) return editor_insert_char('\t');
    // pad to the next shift-width column, not a fixed 4 spaces
    const int rx = cx_to_rx(E.buf().pt.line_text(E.cy), E.cx);
    const int n = opts.shift_width - (rx % opts.shift_width);
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

static bool has_spare_closer(std::string_view rest, char open, char close) {
    int depth = 0;
    for(const char c : rest) {
        if(c == open) ++depth;
        else if(c == close && --depth < 0) return true;
    }
    return false;
}

static bool is_word(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// is `prev` immediately followed by the character that auto-pairing added?
static bool is_empty_pair(char prev, char next) {
    if(!next) return false;
    return closer_for(prev) == next || (is_quote(prev) && prev == next);
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
        for(int i = 0; i < opts.shift_width && !s.empty() && s.back() == ' ';
            ++i)
            s.pop_back();
    return s;
}

void electric_indent() {
    Buffer& B = E.buf();
    const std::string line = B.pt.line_text(E.cy);
    const std::string_view head = std::string_view(line).substr(0, E.cx);
    const std::string_view tok = head.substr(indent::leading_ws(head).size());
    const bool closer = tok.size() == 1 && is_closer(tok[0]);
    if(!closer && std::ranges::find(kElectric, tok) == std::end(kElectric))
        return;
    if(const auto t = indent::from_tree(B.syntax, B.pt, B.eol, E.cy))
        set_line_indent(*t);
    else if(closer) set_line_indent(one_level_less(indent::leading_ws(head)));
}

}  // namespace

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
        const auto t = indent::from_tree(B.syntax, B.pt, B.eol, l);
        return t ? *t : fallback;
    };
    const std::string mid = ws(E.cy, indent::for_newline(before));
    const std::string close = split ? ws(E.cy + 1, base) : std::string{};
    if(split) B.pt.insert(B.pt.line_start(E.cy + 1), close);
    B.pt.insert(B.pt.line_start(E.cy), mid);
    E.cx = static_cast<int>(mid.size());
}

// typing a printable ascii char in insert mode
void editor_type_char(int c) {
    const char ch = static_cast<char>(c);
    const std::string line = E.buf().pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const char prev = cx > 0 ? line[cx - 1] : 0;
    const char next = cx < line.size() ? line[cx] : 0;

    // typing over a closer we (or the user) already put there
    if((is_closer(ch) || is_quote(ch)) && next == ch) {
        E.cx++;
        return;
    }

    // electric dedent: a closer on a whitespace-only line pulls back one level
    if(is_closer(ch)) {
        editor_insert_char(c);
        electric_indent();
        return;
    }

    // only pair when what follows can't be part of the thing we're opening
    const bool room = next == 0 || next == ' ' || next == '\t' ||
                      is_closer(next) || next == ',' || next == ';';
    if(const char cl = closer_for(ch)) {
        const bool pair = room && !has_spare_closer(line, ch, cl);
        E.buf().pt.insert(cursor_offset(),
                          pair ? std::string{ch, cl} : std::string{ch});
        E.cx++;
        return;
    }
    if(is_quote(ch)) {
        const bool apostrophe = ch == '\'' && is_word(prev);  // don't, it's
        const bool odd = std::count(line.begin(), line.end(), ch) % 2 != 0;
        const bool pair = room && !apostrophe && prev != ch && !odd;
        if(pair) {
            E.buf().pt.insert(cursor_offset(), std::string{ch, ch});
            E.cx++;
        } else editor_insert_char(c);
        return;
    }
    editor_insert_char(c);
    if(is_word(ch)) electric_indent();
}

void editor_open_below() {
    Buffer& B = E.buf();
    const std::string heur = indent::for_newline(B.pt.line_text(E.cy));
    B.pt.insert(B.pt.line_end(E.cy), "\n");
    E.cy++;
    const auto t = indent::from_tree(B.syntax, B.pt, B.eol, E.cy);
    const std::string ws = t ? *t : heur;
    B.pt.insert(B.pt.line_start(E.cy), ws);
    E.cx = static_cast<int>(ws.size());
}

void editor_open_above() {
    Buffer& B = E.buf();
    const std::string heur = indent::for_open_above(B.pt.line_text(E.cy));
    B.pt.insert(E.cy > 0 ? B.pt.line_end(E.cy - 1) : 0, "\n");
    const auto t = indent::from_tree(B.syntax, B.pt, B.eol, E.cy);
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
        if(is_empty_pair(line[E.cx - 1], next)) {  // (|) -> |
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
    while(i > 0) {
        const int c = cls(i);
        while(i > 0 && cls(i) == c) --i;
    }
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
    const bool ins = E.mode == INSERT;
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
        indent::reindent_lines(E.buf().syntax, pt, E.buf().eol, from, l1);
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
