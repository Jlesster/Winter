#pragma once

#include <cstddef>
#include <ctime>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "core.hpp"
#include "diag.hpp"
#include "piece.hpp"
#include "syntax.hpp"
#include "virt.hpp"

// data
struct Buffer {
    PieceTable pt{};
    Syntax syntax{};
    std::vector<Diag> diags{};
    DecoStore decos{};
    std::string filename{};
    std::string orig{};
    bool eol = true;
    bool nofile = false;
    int cx = 0;
    int cy = 0;
    int want_rx = 0;
    int row_offset = 0;
    int col_offset = 0;

    void sync_anchors() {
        for(const TextEdit& e : pt.drain_deco_edits()) {
            decos.apply(e);
            if(e.reset) diags.clear();
            else diag_shift(diags, e);
        }
    }
};

struct EditorConfig {
    int cx, cy = 0;
    int rx = 0;
    int want_rx = 0;  // goal column for j/k (INT_MAX = stick to line end)
    int row_offset = 0;
    int col_offset = 0;
    int term_rows = 0;
    int term_cols = 0;
    int num_rows() const { return static_cast<int>(buf().pt.line_count()); }
    std::string status_message = "";
    std::string cmd_prefill;
    time_t status_message_time = 0;
    EditorMode mode = EditorMode::NORMAL;
    size_t cur = 0;
    size_t vm_first = 0;
    size_t vm_last = 0;
    size_t v_anchor = 0;
    bool v_line = false;
    bool vm_valid = false;
    bool cmd_sel = false;
    size_t cmd_sel_start = 0, cmd_sel_end = 0;
    Buffer* alt = nullptr;
    Buffer& buf() const { return *buffers[cur]; }
    std::vector<std::unique_ptr<Buffer>> buffers;
};

extern EditorConfig E;

size_t line_len(size_t y);
size_t cursor_offset();
void set_cursor_offset(size_t pos);
const char* mode_name(EditorMode m);
void set_mode(EditorMode m);

void editor_status_touched();

template <typename... Args>
void editor_set_status_message(std::format_string<Args...> fmt,
                               Args&&... args) {
    E.status_message = std::format(fmt, std::forward<Args>(args)...);
    E.status_message_time = time(nullptr);
    editor_status_touched();
}
