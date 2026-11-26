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

#include <functional>
#include <string>

#include "core.hpp"
#include "registry.hpp"

// Chord state for text-entry modes: insert/replace and the command/search
// prompt. Normal/visual/files keep normal.cpp's state machine.
class Chord {
public:
    enum class Result {
        Pass,      // caller handles this key normally
        Buffered,  // prefix consumed; wait for the next key/timeout
        Handled,   // action ran (or wants_char consumed the key)
    };

    // Replay one pending key as literal text. The caller decides what
    // "literal" means (insert into the buffer, insert into a prompt, ...).
    using Emit = std::function<void(int)>;

    // `buf`: a stable buffer id whose buffer-local maps also apply (0 = none)
    Result feed(EditorMode mode, int key, const Emit& emit_literal,
                uint64_t buf = 0);
    bool pending() const;
    void timeout(const Emit& emit_literal);
    void reset();

private:
    Result feed_empty(EditorMode mode, int key);

    EditorMode mode_ = NORMAL;
    uint64_t buf_ = 0;
    bool have_mode_ = false;
    KeySeq keys_;
    std::string action_;
    std::string arg_;
    bool want_char_ = false;
};
