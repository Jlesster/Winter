#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "virt.hpp"

inline constexpr std::string_view kVisualRange = "V/>>";
inline constexpr int kPrioSubst = 40;

struct SubstView {
    struct Edit {
        size_t lo, hi;
        size_t nlo, nhi;
    };
    std::string text;
    std::vector<LineSpan> marks;
    std::vector<Edit> edits;
    size_t map(size_t off) const;
};

struct CmdSpec;

struct CmdLine {
    bool has_range = false;
    long first = 0, last = 0;
    std::string name;
    bool bang = false;
    std::string args;
    const CmdSpec* spec = nullptr;    // set by the dispatcher
    size_t name_off = 0;              // byte offset of the name in the input line
    size_t args_off = 0;              // byte offset of args in the input line
    std::vector<std::string> argv;    // filled for non-raw commands only
};

using CmdFn = std::function<void(const CmdLine&)>;

enum class Nargs { None, One, Opt, Any };

struct CmdCand {
    std::string text;    // replaces the word being completed
    std::string detail;  // shown dim next to it
};
// `lead` is the part of the current word before the cursor.
using CmdComplete = std::function<std::vector<CmdCand>(std::string_view lead)>;

// Member order matters: designated initializers must follow it.
struct CmdSpec {
    std::string name;  // canonical (long) name
    std::vector<std::string> aliases;
    std::string desc, usage;
    bool range = false, bang = false;
    Nargs nargs = Nargs::Any;
    bool raw = false;  // true: command parses c.args itself
    CmdComplete complete;
    CmdFn run;
};

struct CmdMatch {
    const CmdSpec* spec = nullptr;
    std::vector<std::string> cands;  // filled when the name is ambiguous
};

void command_add(CmdSpec spec);
void command_add(std::string name, CmdFn fn);  // legacy wrapper, Lua uses it
CmdMatch command_find(std::string_view name);

void editor_command_palette(std::string_view cmd);
void editor_command_prompt();
void register_builtin_commands();

void subst_preview_key(std::string cmd, int key);
bool subst_preview_line(int row, std::string_view line, SubstView& out);
