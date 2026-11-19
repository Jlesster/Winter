#include "editor.hpp"
#include "marks.hpp"
#include "reg.hpp"
#include <cstddef>
#include <ctime>
#include <string>
#include "core.hpp"
#include "edit.hpp"
#include "events.hpp"
#include "fold_buf.hpp"
#include "loop.hpp"
#include "motion.hpp"
#include "piece.hpp"

EditorConfig E;

// row helpers
size_t line_len(size_t y) {
    return E.buf().pt.line_end(y) - E.buf().pt.line_start(y);
}

// cursor offset modes
size_t cursor_offset() { return E.buf().pt.from_linecol(E.cy, E.cx); }

void set_cursor_offset(size_t pos) {
    const LineCol lc = E.buf().pt.to_linecol(pos);
    E.cy = static_cast<int>(lc.line);
    E.cx = static_cast<int>(lc.col);
}

const char* mode_name(EditorMode m) {
    switch(m) {
        case FILES: return "files";
        case INSERT: return "insert";
        case TERMINAL: return "terminal";
        case NORMAL: return "normal";
        case VISUAL: return "visual";
        case SEARCH: return "search";
        case OPPEND: return "operator";
        case COMMAND: return "command";
        case REPLACE: return "replace";
    }
    return "?";
}

void buffer_refused_edit() { editor_set_status_message("Buffer is read-only"); }

void set_mode(EditorMode m) {
    if(E.mode == m) return;
    if(insertish(m) && E.buf().pt.locked) return buffer_refused_edit();
    if(E.mode == VISUAL && m == NORMAL) {
        const size_t a = std::min(E.v_anchor, cursor_offset());
        const size_t b = std::max(E.v_anchor, cursor_offset());
        E.vm_first = E.buf().pt.line_of(a);
        E.vm_last = E.buf().pt.line_of(b);
        E.vm_valid = true;
        mark_visual(E.buf().id, a, b);  // '< and '>
        const size_t p = motion::clamp_normal(E.buf().pt, cursor_offset());
        set_cursor_offset(p);
    }
    if(insertish(E.mode) && !insertish(m)) editor_strip_autoindent();
    if(E.mode == INSERT) E.buf().pt.end_group();
    const bool was_ins = insertish(E.mode), will_ins = insertish(m);
    if(was_ins && !will_ins) {
        E.buf().pt.end_group();
        replace_reset();
        reg_insert_end();  // the . register
    }
    if(!was_ins && will_ins) {
        E.buf().pt.begin_group();
        fold_reveal(kFoldInsert);
        reg_insert_begin();
    }
    const EditorMode old = E.mode;
    E.mode = m;
    events_emit("mode_changed", {mode_name(old), mode_name(m)});
}

void editor_status_touched() {
    static TimerId id = 0;
    if(id) loop_cancel(id);
    id = loop_after(5100, [] {});
}

namespace {
struct Note {
    time_t t;
    std::string text;
    int count = 1;
};
std::deque<Note> g_notes;
constexpr size_t kMaxNotes = 500;
}  // namespace

void editor_status_log(std::string_view msg) {
    if(msg.empty()) return;
    const time_t now = time(nullptr);
    if(!g_notes.empty() && g_notes.back().text == msg) {
        ++g_notes.back().count;
        g_notes.back().t = now;
        return;
    }
    g_notes.push_back({now, std::string(msg)});
    if(g_notes.size() > kMaxNotes) g_notes.pop_front();
}

std::string editor_status_history() {
    std::string out;
    for(auto it = g_notes.rbegin(); it != g_notes.rend(); ++it) {
        tm tmv{};
        localtime_r(&it->t, &tmv);
        char ts[16];
        std::strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
        std::string_view rest = it->text;
        for(bool first = true;; first = false) {
            const size_t nl = rest.find('\n');
            if(first) {
                out += '[';
                out += ts;
                out += ']';
            } else out += "           ";
            out += rest.substr(0, nl);
            if(first && it->count > 1)
                out += " (x" + std::to_string(it->count) + ")";
            out += '\n';
            if(nl == std::string_view::npos) break;
            rest.remove_prefix(nl + 1);
        }
    }
    return out;
}
