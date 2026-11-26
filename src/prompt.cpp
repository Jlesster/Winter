/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "prompt.hpp"

#include <chrono>
#include <ctime>
#include <format>
#include <map>
#include <optional>
#include <vector>

#include "chord.hpp"
#include "columns.hpp"
#include "core.hpp"
#include "edit.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "options.hpp"
#include "render.hpp"
#include "state.hpp"
#include "terminal.hpp"
#include "tree.hpp"
#include "utf8.hpp"

namespace {

PromptView g_view;
std::map<std::string, std::vector<std::string>> g_history;  // by prompt text

void ensure_history_loaded() {
    static const bool loaded = [] {
        g_history = state_history_load();
        return true;
    }();
    (void)loaded;
}

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

std::string prompt_history_last(std::string_view prompt) {
    ensure_history_loaded();
    const auto it = g_history.find(std::string(prompt));
    return it == g_history.end() || it->second.empty() ? std::string{}
                                                       : it->second.back();
}

PromptView& prompt_view() { return g_view; }

std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int),
                          std::string_view initial) {
    PromptHooks hooks;
    if(callback)
        hooks.after_key = [callback](const std::string& b, size_t, int k, bool) {
            callback(b, k);
        };
    return editor_prompt(prompt, hooks, initial);
}

std::string editor_prompt(std::string_view prompt, const PromptHooks& hooks,
                          std::string_view initial) {
    struct Active {
        Active() { g_view.active = true; }
        ~Active() { g_view.active = false; }
    } active;

    std::string buf(initial);
    size_t pos = buf.size();  // byte offset of the cursor
    ensure_history_loaded();
    auto& hist = g_history[std::string(prompt)];
    size_t hidx = hist.size();
    std::string typed;  // the half-typed line, kept while browsing history
    const int pw = text_width(prompt);

    Chord chord;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    bool submit = false;  // a replayed <S-CR>/<C-CR>/<A-CR> acts like Enter
    const auto emit_literal = [&](int key) {
        const int c = key_passthrough(key);
        if(c == '\r') {
            submit = true;
            return;
        }
        if(c < 32 || c >= 256) return;
        buf.insert(pos, 1, static_cast<char>(c));
        ++pos;
        hidx = hist.size();  // editing leaves history browsing
        if(hooks.after_key) hooks.after_key(buf, pos, c, true);
    };
    const auto submit_if_requested = [&] {
        if(!submit) return false;
        submit = false;
        if(buf.empty()) return false;  // plain Enter ignores an empty prompt too
        history_add(hist, buf);
        state_history_append(prompt, buf);
        editor_set_status_message("");
        if(hooks.after_key) hooks.after_key(buf, pos, '\r', false);
        return true;
    };

    for(;;) {
        // line outgrew the bar: drop leading chars so the cursor stays visible
        const std::string_view sv = buf;
        size_t first = 0;
        while(first < pos && pw + text_width(sv.substr(first, pos - first)) >=
                                 editor_screen_cols())
            first = gr_next(sv, first);
        E.status_message = std::format("{}{}", prompt, sv.substr(first));
        E.status_message_time = time(nullptr);
        g_view.col = pw + text_width(sv.substr(first, pos - first));
        editor_refresh_screen();

        int wait_ms = -1;
        if(hooks.keymap_mode && chord.pending() && opts.timeoutlen > 0) {
            if(!deadline)
                deadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(opts.timeoutlen);

            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    *deadline - std::chrono::steady_clock::now())
                    .count();

            if(left <= 0) {
                deadline.reset();
                chord.timeout(emit_literal);
                if(submit_if_requested()) return buf;
                continue;
            }
            wait_ms = static_cast<int>(left);
        } else if(!chord.pending()) {
            deadline.reset();
        }

        int c = editor_read_key_timeout(true, wait_ms);
        if(c == NONE) {
            // NONE can also mean "a timer woke the loop", so only resolve the
            // chord when the deadline has really passed.
            if(wait_ms >= 0 && deadline &&
               std::chrono::steady_clock::now() >= *deadline) {
                deadline.reset();
                chord.timeout(emit_literal);
                if(submit_if_requested()) return buf;
            }
            continue;  // resize / unknown escape / chord timeout
        }
        deadline.reset();

        if(hooks.keymap_mode) {
            const Chord::Result r =
                chord.feed(*hooks.keymap_mode, c, emit_literal);
            if(submit_if_requested()) return buf;
            if(r != Chord::Result::Pass) continue;
        }

        if(key_mods(c)) {
            c = key_passthrough(c);
            if(c == NONE) continue;
        }

        if(hooks.pre_key && hooks.pre_key(c, buf, pos)) {
            if(pos > buf.size()) pos = buf.size();
            hidx = hist.size();
            continue;
        }

        if(c == '\x1b') {
            editor_set_status_message("");
            if(hooks.after_key) hooks.after_key(buf, pos, c, false);
            return "";
        }
        if(c == '\r' && !buf.empty()) {
            history_add(hist, buf);
            state_history_append(prompt, buf);
            editor_set_status_message("");
            if(hooks.after_key) hooks.after_key(buf, pos, c, false);
            return buf;
        }

        bool edited = true;  // false: moved / browsed only
        switch(c) {
            case BACKSPACE:
                if(pos > 0) {
                    const size_t p = gr_prev(buf, pos);
                    buf.erase(p, pos - p);
                    pos = p;
                }
                break;
            case DELETE:
                if(pos < buf.size()) buf.erase(pos, gr_next(buf, pos) - pos);
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
                if(pos > 0) pos = gr_prev(buf, pos);
                edited = false;
                break;
            case ARROW_RIGHT:
                if(pos < buf.size()) pos = gr_next(buf, pos);
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
            case PASTE: {
                std::string t = editor_take_paste();
                for(char& c : t)
                    if(static_cast<unsigned char>(c) < 32) c = ' ';
                buf.insert(pos, t);
                pos += t.size();
            } break;
            default:
                if(c >= 32 && c < 256) {  // ASCII and raw UTF-8 bytes
                    buf.insert(pos, 1, static_cast<char>(c));
                    ++pos;
                } else edited = false;
                break;
        }
        if(edited) hidx = hist.size();  // editing leaves history browsing
        if(hooks.after_key) hooks.after_key(buf, pos, c, edited);
    }
}
