#pragma once
// Runtime options. These replaced the WINTER_* #defines so `:set` (and soon
// Lua) can change them while the editor runs.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class CursorShape : uint8_t {
    Default = 0,
    Block = 1,
    Underline = 2,
    Bar = 3
};

struct CursorStyle {
    CursorShape shape = CursorShape::Block;
    bool blink = false;
};

struct Options {
    bool cursor_inverse = false;    // semantic cursor coloring
    bool undofile = true;           // undo saved state per file
    bool number = false;            // line num
    bool relative_number = false;   // relative line num
    bool scrolleof = false;         // scroll past the EOF
    bool expand_tab = true;         // Tab key / indents use spaces
    bool remember_cursor = true;    // cursor pos memory per file
    bool rainbow = true;            // rainbow delimiters
    bool diagnostics = true;        // show LSP diags
    bool diag_virtual_text = true;  // end-of-line message
    bool diag_undercurl = true;     // modern curly linting
    bool hlsearch = true;           // search match highlighting
    bool paste_indent = true;       // indent pastes
    int timeoutlen = 1000;          // chord timeout ms
    int undo_levels = 1000;         // # of stored edits per file
    int diag_severity = 4;  // show this severity and worse: 1 error .. 4 hint
    int scrolloff = 5;      // scroll off padding
    int tab_stop = 4;       // display width of a '\t'
    int shift_width = 4;    // one indent level
    int quit_times = 3;     // extra Ctrl-Q presses needed with unsaved changes
    int history = 500;      // history entries per file
    int number_width = 4;   // gutter width
    // DECSCUSR: 1/2 block, 3/4 underline, 5/6 bar (odd = blinking)
    CursorStyle cursor_normal{CursorShape::Block, false};
    CursorStyle cursor_insert{CursorShape::Bar, false};
    CursorStyle cursor_visual{CursorShape::Underline, false};
};

extern Options opts;

enum class OptResult { Ok, Unknown, BadValue };

// string-keyed access: this is the surface `:set` and Lua talk to.
// Names follow vim where one exists (tabstop/ts, shiftwidth/sw, expandtab/et).
OptResult set_option(std::string_view name, std::string_view value);
std::optional<std::string> get_option(std::string_view name);
std::vector<std::string_view> option_names();

// `:set` argument parsing: "ts=8 noet sw?" -> message to show ("" = nothing)
std::string apply_set_command(std::string_view args);
