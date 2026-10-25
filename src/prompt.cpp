#include "prompt.hpp"

#include <ctime>
#include <format>
#include <map>
#include <vector>

#include "columns.hpp"
#include "core.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "options.hpp"
#include "render.hpp"
#include "state.hpp"
#include "terminal.hpp"
#include "utf8.hpp"

namespace {

PromptView g_view;
std::map<std::string, std::vector<std::string>> g_history;  // by prompt text

void history_add(std::vector<std::string>& h, const std::string& s) {
    std::erase(h, s);  // only the newest copy survives, like vim
    h.push_back(s);
    if(h.size() > static_cast<size_t>(opts.history)) h.erase(h.begin());
}

// idx == h.size() means "the line you were typing". Walks to the next entry
// that starts with `typed`; running off the newest end restores `typed`.
bool history_step(const std::vector<std::string>& h, size_t& idx, int dir,
                  const std::string& typed, std::string& out) {
    size_t i = idx;
    for(;;) {
        if(dir < 0) {
            if(i == 0) return false;
            --i;
        } else {
            if(i >= h.size()) return false;
            ++i;
        }
        if(i == h.size()) {
            out = typed;
            idx = i;
            return true;
        }
        if(h[i].starts_with(typed)) {
            out = h[i];
            idx = i;
            return true;
        }
    }
}

}  // namespace

PromptView& prompt_view() { return g_view; }

std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int),
                          std::string_view initial) {
    struct Active {
        Active() { g_view.active = true; }
        ~Active() { g_view.active = false; }
    } active;

    std::string buf(initial);
    size_t pos = buf.size();  // byte offset of the cursor
    static const bool loaded = [] {
        g_history = state_history_load();
        return true;
    }();
    (void)loaded;
    auto& hist = g_history[std::string(prompt)];
    size_t hidx = hist.size();
    std::string typed;  // the half-typed line, kept while browsing history
    const int pw = text_width(prompt);

    for(;;) {
        // line outgrew the bar: drop leading chars so the cursor stays visible
        const std::string_view sv = buf;
        size_t first = 0;
        while(first < pos &&
              pw + text_width(sv.substr(first, pos - first)) >= E.term_cols)
            first = utf8_next(sv, first);
        E.status_message = std::format("{}{}", prompt, sv.substr(first));
        E.status_message_time = time(nullptr);
        g_view.col = pw + text_width(sv.substr(first, pos - first));
        editor_refresh_screen();

        const int c = editor_read_key();
        if(c == NONE) continue;  // resize / unknown escape: just redraw

        if(c == '\x1b') {
            editor_set_status_message("");
            if(callback) callback(buf, c);
            return "";
        }
        if(c == '\r' && !buf.empty()) {
            history_add(hist, buf);
            state_history_append(prompt, buf);
            editor_set_status_message("");
            if(callback) callback(buf, c);
            return buf;
        }

        bool edited = true;  // false: moved / browsed only
        switch(c) {
            case BACKSPACE:
                if(pos > 0) {
                    const size_t p = utf8_prev(buf, pos);
                    buf.erase(p, pos - p);
                    pos = p;
                }
                break;
            case DELETE:
                if(pos < buf.size()) buf.erase(pos, utf8_next(buf, pos) - pos);
                break;
            case CTRL_BACKSPACE:
            case CTRL_KEY('h'):
            case CTRL_KEY('w'): {
                const size_t p = word_back_stop(buf, pos);
                buf.erase(p, pos - p);
                pos = p;
            } break;
            case CTRL_KEY('u'):
                buf.erase(0, pos);
                pos = 0;
                break;
            case ARROW_LEFT:
                if(pos > 0) pos = utf8_prev(buf, pos);
                edited = false;
                break;
            case ARROW_RIGHT:
                if(pos < buf.size()) pos = utf8_next(buf, pos);
                edited = false;
                break;
            case HOME:
            case CTRL_KEY('a'):
                pos = 0;
                edited = false;
                break;
            case END:
            case CTRL_KEY('e'):
                pos = buf.size();
                edited = false;
                break;
            case ARROW_UP:
            case CTRL_KEY('p'):
            case ARROW_DOWN:
            case CTRL_KEY('n'): {
                const int dir = (c == ARROW_UP || c == CTRL_KEY('p')) ? -1 : +1;
                if(hidx == hist.size()) typed = buf;
                std::string out;
                if(history_step(hist, hidx, dir, typed, out)) {
                    buf = out;
                    pos = buf.size();
                }
                edited = false;
            } break;
            default:
                if(c >= 32 && c < 256) {  // ASCII and raw UTF-8 bytes
                    buf.insert(pos, 1, static_cast<char>(c));
                    ++pos;
                } else edited = false;
                break;
        }
        if(edited) hidx = hist.size();  // editing leaves history browsing
        if(callback) callback(buf, c);
    }
}
