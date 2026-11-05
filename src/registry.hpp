#pragma once
// Everything the editor can *do* is a named action; keys are data that point
// at action names. Lua and operators will use the same tables.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core.hpp"
#include "motion.hpp"

using KeySeq = std::u32string;

struct Range {
    size_t start = 0, end = 0;
    bool linewise = false;
};
using Operator = std::function<void(const Range&)>;
using ObjectFn = std::function<std::optional<Range>(const PieceTable&,
                                                    size_t pos, int count)>;

struct Ctx {
    int count = 0;  // 0 = no count typed
    std::string
        arg;  // the character typed after a wants_char action (f, t, r...)
};
using Action = std::function<void(Ctx&)>;

enum class Lookup { None, Prefix, Exact };
struct Hit {
    Lookup kind = Lookup::None;
    std::string action;  // valid for Exact
    bool longer =
        false;  // a longer chord shares this prefix (wait for more keys)
};

class Registry {
public:
    // ---- actions: name -> behaviour ("edit.undo", "mode.insert", ...)
    void add_action(std::string name, Action fn, bool wants_char = false);
    bool run_action(std::string_view name, Ctx& ctx) const;  // false: unknown
    bool remove_action(std::string_view name);
    bool wants_char(std::string_view name) const;
    std::vector<std::string> action_names() const;

    // ---- motions: pure (text, pos, count) -> Target. Operators need these
    // rather than the cursor-moving action, so they get their own table.
    void add_motion(std::string name, motion::Fn fn);
    motion::Fn motion(std::string_view name) const;  // nullptr if not a motion

    // ---- keymaps: per mode, key sequence -> action name
    void map(EditorMode m, KeySeq keys, std::string action);
    void map(EditorMode m, const char* notation, std::string action);  // "gg"
    bool unmap(EditorMode m, std::u32string_view keys);
    Hit lookup(EditorMode m, std::u32string_view keys) const;

    void add_operator(std::string name, Operator fn);
    const Operator* op(std::string_view name) const;
    void add_object(std::string name, ObjectFn fn);
    ObjectFn object(std::string_view name) const;

private:
    struct Entry {
        Action fn;
        bool wants_char = false;
    };
    std::map<std::string, Entry, std::less<>> actions_;
    std::map<std::string, motion::Fn, std::less<>> motions_;
    std::map<EditorMode, std::map<KeySeq, std::string, std::less<>>> keymaps_;
    std::map<std::string, Operator, std::less<>> ops_;
    std::map<std::string, ObjectFn, std::less<>> objects_;
};

Registry& registry();

// "<C-r>gg<Esc>" -> key sequence. Inside <>: optional C- A- (or M-) S-
// prefixes, then one character or a name: Esc CR Enter Tab BS Space lt Up Down
// Left Right Home End PageUp PageDown Del. Unknown <names> are typed literally.
// An impossible combination (<C-S-a>, <S-1>) returns an empty sequence and
// sets *err to a string literal.
KeySeq parse_keys(std::string_view notation, const char** err = nullptr);

// the inverse, for the status line: {0x12,'g'} -> "<C-r>g"
std::string format_keys(const KeySeq& keys);
