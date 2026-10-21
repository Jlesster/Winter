#include "gutter.hpp"
#include <algorithm>
#include <cstdlib>
#include "options.hpp"

int gutter_width(int line_count) {
    if(!opts.number && !opts.relative_number) return 0;
    int digits = 1;
    for(int n = std::max(line_count, 1); n >= 10; n /= 10) ++digits;
    return std::max(opts.number_width, digits + 1);
}

std::string gutter_label(int line, int cursor_line, int width) {
    if(width <= 0) return {};
    const bool current = line == cursor_line;
    int n = 0;
    bool left_align = false;
    if(opts.relative_number && !current) {
        n = std::abs(line - cursor_line);
    } else if(opts.number) {
        n = line + 1;
        // left_align = opts.relative_number;
    }
    std::string num = std::to_string(n);
    const size_t room = static_cast<size_t>(width) - 1;
    if(num.size() > room) num.erase(0, num.size() - room);
    if(left_align)
        return num + std::string(static_cast<size_t>(width) - num.size(), ' ');
    return std::string(room - num.size(), ' ') + num + ' ';
}
