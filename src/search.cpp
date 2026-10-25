#include "search.hpp"

#include <string>

#include "columns.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "prompt.hpp"

// find

void editor_find_callback(std::string query, int key) {
    static int last_match = -1;
    static std::string saved_query = "";
    int direction = 1;
    if(E.mode == EditorMode::NORMAL && (key == 'n' || key == 'N')) {
        if(saved_query == "") {
            editor_set_status_message("No previous search");
            return;
        }
        query = saved_query;
        direction = (key == 'N') ? -1 : 1;
    } else if(key == '\r') {
        saved_query = query;
        return;
    } else {
        last_match = -1;
        if(key == '\x1b' || query[0] == '\0') return;
    }
    if(last_match == -1) direction = 1;
    if(last_match >= E.num_rows()) last_match = -1;
    int current = last_match;
    int i;
    for(i = 0; i < E.num_rows(); i++) {
        current += direction;
        if(current == -1) current = E.num_rows() - 1;
        else if(current == E.num_rows()) current = 0;
        const std::string line = E.buf().pt.line_text(current);
        size_t match_pos = line.find(query);
        if(match_pos != std::string::npos) {
            last_match = current;
            E.cy = current;
            E.cx = static_cast<int>(match_pos);
            E.row_offset = E.num_rows();
            return;
        }
    }
    editor_set_status_message("Pattern not found: {}", query);
}

void editor_find() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_col_offset = E.col_offset;
    int saved_row_offset = E.row_offset;
    std::string query = editor_prompt("Search: ", editor_find_callback);
    set_mode(EditorMode::NORMAL);
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.col_offset = saved_col_offset;
    E.row_offset = saved_row_offset;
}
