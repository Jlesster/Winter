#pragma once

#include <ctime>
#include <format>
#include <string>
#include <string_view>

#include "core.hpp"
#include "piece.hpp"
#include "syntax.hpp"

struct EditorConfig {
    bool eol = true;
    int cx, cy = 0;
    int rx = 0;
    int want_rx = 0;
    int row_offset = 0;
    int col_offset = 0;
    int term_rows = 0;
    int term_cols = 0;
    std::string orig{};
    PieceTable pt{};
    Syntax syntax{};
    int num_rows() const { return static_cast<int>(pt.line_count()); }
    std::string filename = "";
    std::string status_message = "";
    time_t status_message_time = 0;
    EditorMode mode = EditorMode::NORMAL;
};

extern EditorConfig E;

size_t line_len(size_t y);
size_t cursor_offset();
void set_cursor_offset(size_t pos);
void set_mode(EditorMode m);

template <typename... Args>
void editor_set_status_message(std::format_string<Args...> fmt,
                               Args&&... args) {
    E.status_message = std::format(fmt, std::forward<Args>(args)...);
    E.status_message_time = time(nullptr);
}
