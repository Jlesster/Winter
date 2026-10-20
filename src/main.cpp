#include "editor.hpp"
#include "file.hpp"
#include "input.hpp"
#include "render.hpp"
#include "terminal.hpp"

void init_editor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.filename = "";
    E.status_message.clear();
    E.status_message_time = 0;
    int rows, cols;
    if(get_window_size(&rows, &cols) == -1) die("get_window_size");
    render_resize(rows, cols);
}

int main(int argc, char* argv[]) {
    enable_raw_mode();
    init_editor();
    install_winch_handler();
    if(argc >= 2) { editor_open(argv[1]); }
    editor_set_status_message(
        "HELP: CTRL-S/:w = save | CTRL-Q/:q = quit | / = search");
    // if(auto s = E.syntax.summary(); !s.empty())
    //     editor_set_status_message("{}", s);
    while(1) {
        editor_refresh_screen();
        editor_process_key();
    }
    return 0;
}
