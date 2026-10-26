#include "indent.hpp"
#include <cstddef>
#include <optional>
#include "options.hpp"
#include "piece.hpp"
#include "syntax.hpp"

namespace indent {
std::string unit() {
    return opts.expand_tab
               ? std::string(static_cast<size_t>(opts.shift_width), ' ')
               : std::string("\t");
}

std::string_view leading_ws(std::string_view line) {
    size_t i = 0;
    while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.substr(0, i);
}

std::string ws_for_cols(int cols) {
    if(cols <= 0) return {};
    if(opts.expand_tab) return std::string(static_cast<size_t>(cols), ' ');
    return std::string(static_cast<size_t>(cols / opts.tab_stop), '\t') +
           std::string(static_cast<size_t>(cols % opts.tab_stop), ' ');
}

static bool is_opener(char c) { return c == '(' || c == '[' || c == '{'; }

std::string for_newline(std::string_view before) {
    std::string out(leading_ws(before));
    const size_t last = before.find_last_not_of(" \t");
    if(last != std::string_view::npos && is_opener(before[last])) out += unit();
    return out;
}

std::string for_open_above(std::string_view line) {
    return std::string(leading_ws(line));
}

void reindent_lines(Syntax& syn, PieceTable& pt, bool eol, size_t l0,
                    size_t l1) {
    PieceTable::Group g(pt);
    for(size_t l = l0; l <= l1; ++l) {
        const std::string line = pt.line_text(l);
        if(line.find_first_not_of(" \t") == std::string::npos) continue;
        const auto ws = from_tree(syn, pt, eol, l);
        if(!ws) continue;
        const std::string_view cur = leading_ws(line);
        if(cur == *ws) continue;
        const size_t at = pt.line_start(l);
        if(!cur.empty()) pt.remove(at, cur.size());
        if(!ws->empty()) pt.insert(at, *ws);
    }
}

std::optional<std::string> from_tree(Syntax& syn, PieceTable& pt, bool eol,
                                     size_t line) {
    syn.update(pt, eol);
    const auto cols = syn.indent_for(line);
    if(!cols) return std::nullopt;
    return ws_for_cols(*cols);
}

}  // namespace indent
