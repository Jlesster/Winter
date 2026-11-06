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

enum class Border : uint8_t { Rounded, Straight, Double, Block };
std::optional<Border> border_from_name(std::string_view s);

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
    bool semantic_tokens = true;    // lsp semantic token highlighting
    bool hlsearch = true;           // search match highlighting
    bool paste_indent = true;       // indent pastes
    bool nerdicons = false;         // ui nerd icon toggle
    bool tree_icons = false;     // no ui icons aside from the tree box glyphs
    bool tree_hidden = false;    // show dotfiles when the tree is first opened
    bool tree_follow = true;     // keep the cursor on the active buffer's file
    bool tree_git = true;        // git status marks
    bool tree_git_names = true;  // recolor file names by git stats
    bool wrap = false;           // wrap long lines at screen edge
    bool hl_reference =
        false;  // highlight other uses of the symbol under the cursor (LSP)
    bool cursorline = false;      // tint the cursor row
    bool matchparen = false;      // highlight the matching bracket
    bool hl_trailing = false;     // highlight trailing whitespace
    bool format_on_save = false;  // run the formatter before every write
    bool hot_reload = true;       // config auto reload on file watch
    int indent_namespace = 2;     // 0 flat, 1 indent 2 follow block first line
    int indent_case = 2;          // 0 flat, 1 indent 2 follow block first line
    int colorcolumn = 0;          // 1-based display column to tint, 0 = off
    int timeoutlen = 1000;        // chord timeout ms
    int undo_levels = 1000;       // # of stored edits per file
    int diag_severity = 4;  // show this severity and worse: 1 error .. 4 hint
    int scrolloff = 5;      // scroll off padding
    int tab_stop = 4;       // display width of a '\t'
    int shift_width = 4;    // one indent level
    int format_timeout = 1000;  // ms a formatter may block a save
    int quit_times = 3;    // extra Ctrl-Q presses needed with unsaved changes
    int history = 500;     // history entries per file
    int number_width = 4;  // gutter width
    int tree_width = 30;   // columns, before the separator
    int indent_engine =
        0;  // 0 structural + ts fallback / 1 ts only / 2 structural only
    Border border = Border::Rounded;  // ui border selection
    // DECSCUSR: 1/2 block, 3/4 underline, 5/6 bar (odd = blinking)
    CursorStyle cursor_normal{CursorShape::Block, false};
    CursorStyle cursor_insert{CursorShape::Bar, false};
    CursorStyle cursor_visual{CursorShape::Underline, false};
    CursorStyle cursor_replace{CursorShape::Underline, false};
};

// Per-buffer overrides of the indent options. nullopt = use the global value.
struct BufOpts {
    std::optional<int> tab_stop, shift_width;
    std::optional<bool> expand_tab;
};

extern Options opts;

enum class OptResult { Ok, Unknown, BadValue, NotLocal };

// effective values for the current buffer (its override, else the global)
int opt_ts();
int opt_sw();
bool opt_et();

// buffer-local write/read; only tabstop, shiftwidth and expandtab have a
// local form (anything else -> OptResult::NotLocal)
struct Buffer;
OptResult set_option_local(Buffer& b, std::string_view name, std::string_view value);
std::optional<std::string> get_option_local(const Buffer& b, std::string_view name);

// string-keyed access: this is the surface `:set` and Lua talk to.
// Names follow vim where one exists (tabstop/ts, shiftwidth/sw, expandtab/et).
OptResult set_option(std::string_view name, std::string_view value);
std::optional<std::string> get_option(std::string_view name);
std::vector<std::string_view> option_names();

struct OptInfo {
    std::string_view name, alias;
    std::string_view type;  // "boolean" | "integer" | "cursor" | "border"
    int lo = 0, hi = 0;     // integer range
};
std::optional<OptInfo> option_info(std::string_view name);

// `:set` argument parsing: "ts=8 noet sw?" -> message to show ("" = nothing)
std::string apply_set_command(std::string_view args);
