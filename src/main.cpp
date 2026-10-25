#include "buflist.hpp"
#include "builtin.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "file.hpp"
#include "input.hpp"
#include "job.hpp"
#include "loop.hpp"
#include "render.hpp"
#include "script.hpp"
#include "terminal.hpp"

void init_editor() {
    E.buffers.push_back(std::make_unique<Buffer>());
    E.cur = 0;
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.status_message.clear();
    E.status_message_time = 0;
    int rows, cols;
    if(get_window_size(&rows, &cols) == -1) die("get_window_size");
    render_resize(rows, cols);
}

int main(int argc, char* argv[]) {
    enable_raw_mode();
    init_editor();
    loop_init();
    job_init();
    install_winch_handler();
    register_builtins();
    const bool lua_ok = script_init();
    if(lua_ok) script_load_config();
    for(int i = 1; i < argc; ++i) buffer_open(argv[i]);
    if(argc >= 2) buffer_open(argv[1]);
    editor_set_status_message(
        "HELP: CTRL-S/:w = save | CTRL-Q/:q = quit | / = search");
    if(const std::string& err = script_last_error(); !err.empty())
        editor_set_status_message("Lua: {}", err.substr(0, err.find('\n')));
    if(lua_ok) events_emit("ready");
    while(1) {
        editor_refresh_screen();
        editor_process_key();
    }
    return 0;
}
