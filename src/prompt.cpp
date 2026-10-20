#include "prompt.hpp"

#include <ctime>
#include <format>

#include "core.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "render.hpp"
#include "terminal.hpp"

// input
std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int)) {
    std::string buf;
    while(1) {
        E.status_message = std::format("{}{}", prompt, buf);
        E.status_message_time = time(nullptr);
        editor_refresh_screen();
        int c = editor_read_key();
        if(c == NONE) continue;  // resize / unknown escape: just redraw
        if(c == DELETE || c == CTRL_KEY('h') || c == BACKSPACE) {
            if(!buf.empty()) buf.pop_back();
        } else if(c == '\x1b') {
            editor_set_status_message("");
            if(callback) callback(buf.c_str(), c);
            return "";
        } else if(c == '\r') {
            if(!buf.empty()) {
                editor_set_status_message("");
                if(callback) callback(buf, c);
                return buf;
            }
        } else if(!iscntrl(c) && c < 128) {
            buf.push_back(static_cast<char>(c));
        }
        if(callback) callback(buf, c);
    }
}
