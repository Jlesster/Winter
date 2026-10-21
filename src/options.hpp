#pragma once
// Runtime options. These replaced the WINTER_* #defines so `:set` (and soon
// Lua) can change them while the editor runs.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct Options {
    int tab_stop = 4;       // display width of a '\t'
    int shift_width = 4;    // one indent level
    bool expand_tab = true; // Tab key / indents use spaces
    int quit_times = 3;     // extra Ctrl-Q presses needed with unsaved changes
    // DECSCUSR: 1/2 block, 3/4 underline, 5/6 bar (odd = blinking)
    int cursor_normal = 2;
    int cursor_insert = 6;
    int cursor_visual = 4;
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
