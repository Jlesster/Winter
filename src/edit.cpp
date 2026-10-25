#include "edit.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "options.hpp"
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

// auto indent / auto pairs

// one indent level, per the current options
std::string indent_unit() {
    return opts.expand_tab
               ? std::string(static_cast<size_t>(opts.shift_width), ' ')
               : std::string("\t");
}

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

static std::string_view leading_ws(std::string_view line) {
    size_t i = 0;
    while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.substr(0, i);
}

// is `prev` immediately followed by the character that auto-pairing added?
static bool is_empty_pair(char prev, char next) {
    if(!next) return false;
    return closer_for(prev) == next || (is_quote(prev) && prev == next);
}

}  // namespace

void editor_insert_newline() {
    const std::string line = E.buf().pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const std::string_view before = std::string_view(line).substr(0, cx);
    const std::string_view after = std::string_view(line).substr(cx);

    const std::string indent(leading_ws(before));
    const size_t last = before.find_last_not_of(" \t");
    const char prev = last == std::string_view::npos ? 0 : before[last];

    // cursor sat in pure whitespace: don't leave trailing whitespace behind
    if(last == std::string_view::npos && cx > 0) {
        E.buf().pt.remove(cursor_offset() - cx, cx);
        E.cx = 0;
    }

    std::string ins = "\n" + indent;
    size_t new_cx = indent.size();
    if(const char cl = closer_for(prev)) {
        const std::string unit = indent_unit();
        ins += unit;
        new_cx += unit.size();
        // {|} -> split onto three lines, cursor on the middle one
        if(after.starts_with(cl)) ins += "\n" + indent;
    }
    E.buf().pt.insert(cursor_offset(), ins);
    E.cy++;
    E.cx = static_cast<int>(new_cx);
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
        if(line.find_first_not_of(" \t") >= cx) {
            size_t drop = 0;
            if(prev == '\t') drop = 1;
            else
                while(drop < static_cast<size_t>(opts.shift_width) &&
                      drop < cx && line[cx - 1 - drop] == ' ')
                    ++drop;
            if(drop) {
                E.buf().pt.remove(cursor_offset() - drop, drop);
                E.cx -= static_cast<int>(drop);
            }
        }
        editor_insert_char(c);
        return;
    }

    // only pair when what follows can't be part of the thing we're opening
    const bool room = next == 0 || next == ' ' || next == '\t' ||
                      is_closer(next) || next == ',' || next == ';';
    if(const char cl = closer_for(ch)) {
        E.buf().pt.insert(cursor_offset(),
                          room ? std::string{ch, cl} : std::string{ch});
        E.cx++;
        return;
    }
    if(is_quote(ch)) {
        const bool apostrophe = ch == '\'' && is_word(prev);  // don't, it's
        const bool pair = room && !apostrophe && prev != ch;
        if(pair) {
            E.buf().pt.insert(cursor_offset(), std::string{ch, ch});
            E.cx++;
        } else editor_insert_char(c);
        return;
    }
    editor_insert_char(c);
}

namespace {

// o / O: open a fresh line below / above the cursor line

// indentation for a line opened *below* `line`: same as it, one deeper if
// it ends in an opener
static std::string indent_below(std::string_view line) {
    std::string indent(leading_ws(line));
    const size_t last = line.find_last_not_of(" \t");
    if(last != std::string_view::npos && closer_for(line[last]))
        indent += indent_unit();
    return indent;
}

}  // namespace

void editor_open_below() {
    const std::string indent = indent_below(E.buf().pt.line_text(E.cy));
    E.buf().pt.insert(E.buf().pt.line_end(E.cy), "\n" + indent);
    E.cy++;
    E.cx = static_cast<int>(indent.size());
}

void editor_open_above() {
    const std::string indent(leading_ws(E.buf().pt.line_text(E.cy)));
    E.buf().pt.insert(E.buf().pt.line_start(E.cy), indent + "\n");
    // the new line took this line's index; the old text moved down one
    E.cx = static_cast<int>(indent.size());
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
