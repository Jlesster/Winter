#include "command.hpp"

#include <string>

#include "editor.hpp"
#include "file.hpp"
#include "options.hpp"
#include "prompt.hpp"
#include "script.hpp"
#include "terminal.hpp"

void editor_command_palette(std::string_view cmd) {
    while(cmd.starts_with(' ')) cmd.remove_prefix(1);
    while(cmd.ends_with(' ')) cmd.remove_suffix(1);
    if(cmd == "q" || cmd == "quit") {
        if(E.pt.modified()) {
            editor_set_status_message(
                "[WARNING]!! File has unsaved changes, q! | quit! to quit "
                "without saving.");
            return;
        }
        editor_quit();
    } else if(cmd == "q!" || cmd == "quit!") {
        editor_quit();
    } else if(cmd == "w" || cmd == "write") {
        editor_save();
    } else if(cmd == "wq" || cmd == "x") {
        editor_save();
        if(!E.pt.modified()) editor_quit();
    } else if(cmd.starts_with("lua ")) {
        const std::string out = script_eval(cmd.substr(4));
    } else if(cmd == "set" || cmd.starts_with("set ")) {
        // :set ts=8  :set noet  :set sw?
        const std::string msg = apply_set_command(cmd.substr(3));
        if(!msg.empty()) editor_set_status_message("{}", msg);
    } else {
        editor_set_status_message("Unknown Command: {}", cmd);
    }
}

void editor_command_prompt() {
    std::string cmd = editor_prompt(":", nullptr);
    set_mode(EditorMode::NORMAL);
    if(cmd == "") return;
    editor_command_palette(cmd);
    cmd.clear();
}
