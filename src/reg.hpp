#pragma once
// Vim-style registers.
//
//   "      unnamed: what a plain y/d/c/p uses; mirrors the system clipboard
//   0      last yank            1-9   last deletes, newest in 1
//   -      last small delete    a-z   named; A-Z append to a-z
//   _      black hole           + *   the system clipboard
//   %      current file name (read only)
//   .      the last inserted text (read only)
//   :      the last command line (read only)
//   /      the last search pattern (read only)
//
// "x before a command picks the register for it ("ayy, "_dd, "+p); in insert
// mode <C-r>x inserts one.
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct RegValue {
    std::string text;
    bool linewise = false;  // linewise text always ends in '\n'
};

// ---- the editor side ------------------------------------------------------------
// Names a user may type after `"` or <C-r>.
bool reg_name_ok(char c);

// The register the running command was told to use (0 = none). The normal-mode
// dispatcher opens one around every command; scopes nest (dot-repeat).
class RegScope {
    char prev_;

public:
    explicit RegScope(char name);
    ~RegScope();
    RegScope(const RegScope&) = delete;
    RegScope& operator=(const RegScope&) = delete;
};
char reg_selected();

enum class RegKind { Yank, Delete };
// Record text a yank / delete / change took. Obeys the selected register,
// otherwise fills 0 (yank) or 1-9 / - (delete) like vim, and "".
void reg_store(RegKind kind, std::string text, bool linewise);

// What p / <C-r> read: `name`, else the selected register, else the unnamed
// one. Text copied outside the editor (the clipboard changed) wins for "".
// Under mc_each, a register that every cursor filled by its own y / d / c hands
// each cursor back its own text (see reg_mc_slot).
RegValue reg_read(char name = 0);

// ---- multicursor ------------------------------------------------------------------
// Inside mc_each: the text *this* cursor put in `name` (0 = the selected or
// unnamed register) on the last pass that wrote it, if every cursor did. Only
// the unnamed and the a-z registers are per cursor; A-Z, digits, + and * are
// not (an A-Z append keeps adding up across the cursors, as before).
std::optional<RegValue> reg_mc_slot(char name = 0);
// Drop the per-cursor texts (the cursors are gone).
void reg_mc_forget();

// ---- scripting side ---------------------------------------------------------------
std::optional<RegValue> reg_get(char name);  // nullopt: empty or unknown
// Sets a register without touching "" (except for name '"', which behaves like
// a yank into the unnamed register). A-Z append. False for read-only/unknown.
bool reg_put(char name, std::string text, bool linewise);
std::vector<char> reg_list();  // registers holding something, in display order

// ---- the . register: the text typed in the last insert session ---------------
// The editor reports what the user types between entering and leaving insert
// mode. An empty session leaves the previous text in place.
enum class RegErase { Char, Word, All };
void reg_insert_begin();
void reg_insert_typed(std::string_view text);  // typed or pasted text
void reg_insert_erase(RegErase what);          // backspace, CTRL-W, CTRL-U
void reg_insert_end();
std::string reg_last_insert();

// Read-only registers computed on demand ('%' is registered by the editor).
void reg_provider(char name, std::function<std::string()> fn);
