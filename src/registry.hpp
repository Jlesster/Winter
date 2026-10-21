#pragma once
// Everything the editor can *do* is a named action; keys are data that point
// at action names. Lua and operators will use the same tables.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core.hpp"
#include "motion.hpp"

struct Ctx {
    int count = 0;    // 0 = no count typed
    std::string arg;  // the character typed after a wants_char action (f, t, r...)
};
using Action = std::function<void(Ctx&)>;

enum class Lookup { None, Prefix, Exact };
struct Hit {
    Lookup kind = Lookup::None;
    std::string action;  // valid for Exact
    bool longer = false; // a longer chord shares this prefix (wait for more keys)
};

class Registry {
  public:
    // ---- actions: name -> behaviour ("edit.undo", "mode.insert", ...)
    void add_action(std::string name, Action fn, bool wants_char = false);
    bool run_action(std::string_view name, Ctx& ctx) const;  // false: unknown
    bool wants_char(std::string_view name) const;
    std::vector<std::string> action_names() const;

    // ---- motions: pure (text, pos, count) -> Target. Operators need these
    // rather than the cursor-moving action, so they get their own table.
    void add_motion(std::string name, motion::Fn fn);
    motion::Fn motion(std::string_view name) const;  // nullptr if not a motion

    // ---- keymaps: per mode, key bytes -> action name
    void map(EditorMode m, std::string keys, std::string action);
    bool unmap(EditorMode m, std::string_view keys);
    Hit lookup(EditorMode m, std::string_view keys) const;

  private:
    struct Entry {
        Action fn;
        bool wants_char = false;
    };
    std::map<std::string, Entry, std::less<>> actions_;
    std::map<std::string, motion::Fn, std::less<>> motions_;
    std::map<EditorMode, std::map<std::string, std::string, std::less<>>> keymaps_;
};

Registry& registry();

// "<C-r>gg<Esc>" -> raw key bytes. Names: C-x, Esc, CR, Tab, BS, Space, lt.
std::string parse_keys(std::string_view notation);
