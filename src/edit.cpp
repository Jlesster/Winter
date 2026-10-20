#include "edit.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "utf8.hpp"

void editor_insert_char(int c) {
    const char ch = static_cast<char>(c);
    E.pt.insert(cursor_offset(), std::string_view(&ch, 1));
    E.cx++;
}

void editor_insert_tab() {
#if WINTER_EXPAND_TAB
    const int rx = cx_to_rx(E.pt.line_text(E.cy), E.cx);
    const int n = WINTER_SHIFT_WIDTH - (rx % WINTER_SHIFT_WIDTH);
    E.pt.insert(cursor_offset(), std::string(static_cast<size_t>(n), ' '));
    E.cx += n;
#else
    editor_insert_char('\t');
#endif
}

namespace {

static const std::string kIndentUnit =
    WINTER_EXPAND_TAB ? std::string(WINTER_SHIFT_WIDTH, ' ')
                      : std::string("\t");

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

static bool is_empty_pair(char prev, char next) {
    if(!next) return false;
    return closer_for(prev) == next || (is_quote(prev) && prev == next);
}

}  // namespace

void editor_insert_newline() {
    const std::string line = E.pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const std::string_view before = std::string_view(line).substr(0, cx);
    const std::string_view after = std::string_view(line).substr(cx);

    const std::string indent(leading_ws(before));
    const size_t last = before.find_last_not_of(" \t");
    const char prev = last == std::string_view::npos ? 0 : before[last];

    if(last == std::string_view::npos && cx > 0) {
        E.pt.remove(cursor_offset() - cx, cx);
        E.cx = 0;
    }

    std::string ins = "\n" + indent;
    size_t new_cx = indent.size();
    if(const char cl = closer_for(prev)) {
        ins += kIndentUnit;
        new_cx += kIndentUnit.size();
        if(after.starts_with(cl)) ins += "\n" + indent;
    }
    E.pt.insert(cursor_offset(), ins);
    E.cy++;
    E.cx = static_cast<int>(new_cx);
}

void editor_type_char(int c) {
    const char ch = static_cast<char>(c);
    const std::string line = E.pt.line_text(E.cy);
    const size_t cx = static_cast<size_t>(E.cx);
    const char prev = cx > 0 ? line[cx - 1] : 0;
    const char next = cx < line.size() ? line[cx] : 0;

    if((is_closer(ch) || is_quote(ch)) && next == ch) {
        E.cx++;
        return;
    }

    if(is_closer(ch)) {
        if(line.find_first_not_of(" \t") >= cx) {
            size_t drop = 0;
            if(prev == '\t') drop = 1;
            else
                while(drop < kIndentUnit.size() && drop < cx &&
                      line[cx - 1 - drop] == ' ')
                    ++drop;
            if(drop) {
                E.pt.remove(cursor_offset() - drop, drop);
                E.cx -= static_cast<int>(drop);
            }
        }
        editor_insert_char(c);
        return;
    }

    const bool room = next == 0 || next == ' ' || next == '\t' ||
                      is_closer(next) || next == ',' || next == ';';
    if(const char cl = closer_for(ch)) {
        E.pt.insert(cursor_offset(),
                    room ? std::string{ch, cl} : std::string{ch});
        E.cx++;
        return;
    }
    if(is_quote(ch)) {
        const bool apostrophe = ch == '\'' && is_word(prev);
        const bool pair = room && !apostrophe && prev != ch;
        if(pair) {
            E.pt.insert(cursor_offset(), std::string{ch, ch});
            E.cx++;
        } else editor_insert_char(c);
        return;
    }
    editor_insert_char(c);
}

namespace {

static std::string indent_below(std::string_view line) {
    std::string indent(leading_ws(line));
    const size_t last = line.find_last_not_of(" \t");
    if(last != std::string_view::npos && closer_for(line[last]))
        indent += kIndentUnit;
    return indent;
}

}  // namespace

void editor_open_below() {
    const std::string indent = indent_below(E.pt.line_text(E.cy));
    E.pt.insert(E.pt.line_end(E.cy), "\n" + indent);
    E.cy++;
    E.cx = static_cast<int>(indent.size());
}

void editor_open_above() {
    const std::string indent(leading_ws(E.pt.line_text(E.cy)));
    E.pt.insert(E.pt.line_start(E.cy), indent + "\n");
    E.cx = static_cast<int>(indent.size());
}

void editor_delete_char() {
    const size_t pos = cursor_offset();
    if(pos == 0) return;
    if(E.cx > 0) {
        const std::string line = E.pt.line_text(E.cy);
        const char next = E.cx < static_cast<int>(line.size()) ? line[E.cx] : 0;
        if(is_empty_pair(line[E.cx - 1], next)) {
            E.pt.remove(pos - 1, 2);
            E.cx--;
            return;
        }
        const int from = static_cast<int>(utf8_prev(line, E.cx));
        E.pt.remove(pos - (E.cx - from), E.cx - from);
        E.cx = from;
    } else {
        const int prev_len = static_cast<int>(line_len(E.cy - 1));
        E.pt.remove(pos - 1, 1);
        E.cy--;
        E.cx = prev_len;
    }
}

void editor_delete_forward() {
    const std::string line = E.pt.line_text(E.cy);
    const size_t pos = cursor_offset();
    if(E.cx < static_cast<int>(line.size())) {
        const int to = static_cast<int>(utf8_next(line, E.cx));
        E.pt.remove(pos, static_cast<size_t>(to - E.cx));
    } else if(E.cy < E.num_rows() - 1) {
        E.pt.remove(pos, 1);
    }
}
