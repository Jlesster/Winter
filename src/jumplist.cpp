#include "jumplist.hpp"
#include <algorithm>
#include <filesystem>
#include <vector>
#include "buflist.hpp"
#include "editor.hpp"
#include "motion.hpp"
#include "piece.hpp"

namespace fs = std::filesystem;

namespace {

struct Spot {
    std::string file;
    size_t line = 0, col = 0;
};

constexpr size_t kMax = 100;
std::vector<Spot> g_list;
size_t g_pos = 0;  // == g_list.size() means "live": not browsing history

bool same_file(const std::string& a, const std::string& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}

bool trackable(const Buffer& b) { return !b.nofile && !b.filename.empty(); }

Spot here() {
    const PieceTable& pt = E.buf().pt;
    const size_t off = cursor_offset();
    const size_t line = pt.line_of(off);
    return {E.buf().filename, line, off - pt.line_start(line)};
}

bool same_line(const Spot& a, const Spot& b) {
    return a.line == b.line && same_file(a.file, b.file);
}

}  // namespace

bool jump_open(const std::string& path) {
    buffer_open(path);
    const Buffer& b = E.buf();
    return !b.filename.empty() && same_file(b.filename, path);
}

bool jump_goto(const std::string& path, size_t line, size_t col) {
    if(!jump_open(path)) return false;
    const PieceTable& pt = E.buf().pt;
    line = std::min(line, pt.line_count() - 1);
    const size_t ls = pt.line_start(line), le = pt.line_end(line);
    size_t off = ls + std::min(col, le - ls);
    // never land inside a UTF-8 sequence
    while(off > ls && off < le &&
          (static_cast<unsigned char>(pt.text(off, 1)[0]) & 0xC0) == 0x80)
        --off;
    set_cursor_offset(motion::clamp_normal(pt, off));
    return true;
}

void jump_push() {
    if(!trackable(E.buf())) return;
    const Spot s = here();
    if(g_pos < g_list.size()) {
        // we were browsing history: a fresh jump discards the forward half
        g_list.resize(g_pos + 1);
        g_list[g_pos] = s;
    } else if(!g_list.empty() && same_line(g_list.back(), s)) {
        g_list.back() = s;  // same line as last time: just refresh the column
    } else {
        g_list.push_back(s);
    }
    if(g_list.size() > kMax) g_list.erase(g_list.begin());
    g_pos = g_list.size();
}

void jump_older(int count) {
    if(g_list.empty()) {
        editor_set_status_message("jumplist: empty");
        return;
    }
    if(g_pos == g_list.size() && trackable(E.buf())) {
        // leaving the live spot: store it so <C-i> can come back to it
        const Spot s = here();
        if(same_line(g_list.back(), s)) g_list.back() = s;
        else g_list.push_back(s);
        g_pos = g_list.size() - 1;
    }
    const size_t n = static_cast<size_t>(std::max(count, 1));
    const size_t want = g_pos >= n ? g_pos - n : 0;
    if(want == g_pos) {
        editor_set_status_message("jumplist: at the oldest entry");
        return;
    }
    g_pos = want;
    const Spot s = g_list[g_pos];
    if(!jump_goto(s.file, s.line, s.col))
        editor_set_status_message("jumplist: cannot open {}", s.file);
}

void jump_newer(int count) {
    if(g_pos + 1 >= g_list.size()) {
        editor_set_status_message("jumplist: at the newest entry");
        return;
    }
    g_pos = std::min(g_pos + static_cast<size_t>(std::max(count, 1)),
                     g_list.size() - 1);
    const Spot s = g_list[g_pos];
    if(!jump_goto(s.file, s.line, s.col))
        editor_set_status_message("jumplist: cannot open {}", s.file);
}
