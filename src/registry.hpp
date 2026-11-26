/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
// Everything the editor can *do* is a named action; keys are data that point
// at action names. Lua and operators will use the same tables.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core.hpp"
#include "motion.hpp"
#include "piece.hpp"

using KeySeq = std::u32string;

struct Range {
    size_t start = 0, end = 0;
    bool linewise = false;
};
using Operator = std::function<void(const Range&)>;
using ObjectFn = std::function<std::optional<Range>(
    const PieceTable&, size_t pos, int count)>;

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
    // `buf` (a stable Buffer::id, 0 = none) adds that buffer's own maps on top:
    // a local map shadows the global one for the same keys, and a longer chord
    // in either keeps the shorter one waiting.
    Hit lookup(EditorMode m, std::u32string_view keys, uint64_t buf = 0) const;

    // ---- buffer-local keymaps (`buf` must not be 0)
    void map_local(uint64_t buf, EditorMode m, KeySeq keys, std::string action);
    bool unmap_local(uint64_t buf, EditorMode m, std::u32string_view keys);
    Hit
    lookup_local(uint64_t buf, EditorMode m, std::u32string_view keys) const;
    // Forget every local map of `buf` (it was closed); returns the actions they
    // pointed at, one per map, so anonymous ones can be released.
    std::vector<std::string> drop_local(uint64_t buf);

    struct MapEntry {
        EditorMode mode;
        KeySeq keys;
        std::string action;
    };
    std::vector<MapEntry> maps(EditorMode m) const;        // the global ones
    std::vector<MapEntry> local_maps(uint64_t buf) const;  // one buffer's

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
    std::map<uint64_t,
             std::map<EditorMode, std::map<KeySeq, std::string, std::less<>>>>
        local_;
    std::map<std::string, Operator, std::less<>> ops_;
    std::map<std::string, ObjectFn, std::less<>> objects_;
};

Registry& registry();

void registry_snapshot();
void registry_restore();

// "<C-r>gg<Esc>" -> key sequence. Inside <>: optional C- A- (or M-) S-
// prefixes, then one character or a name: Esc CR Enter Tab BS Space lt Up Down
// Left Right Home End PageUp PageDown Del. Unknown <names> are typed literally.
// An impossible combination (<C-S-a>, <S-1>) returns an empty sequence and
// sets *err to a string literal.
KeySeq parse_keys(std::string_view notation, const char** err = nullptr);

// the inverse, for the status line: {0x12,'g'} -> "<C-r>g"
std::string format_keys(const KeySeq& keys);
