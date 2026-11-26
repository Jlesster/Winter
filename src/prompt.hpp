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

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core.hpp"

struct PromptView {
    bool active = false;
    int col = 0;
};
PromptView& prompt_view();

struct PromptHooks {
    // Keymaps to use while reading (COMMAND / SEARCH). Empty = plain text.
    std::optional<EditorMode> keymap_mode;

    // Sees every key before the prompt does. Return true to consume it; it
    // may edit `buf` / `pos`. after_key is NOT called for consumed keys.
    std::function<bool(int key, std::string& buf, size_t& pos)> pre_key;
    // Runs after every key the prompt handled itself, including Esc / Enter
    // (right before the prompt returns). `edited` is false for cursor moves
    // and history browsing.
    std::function<void(const std::string& buf, size_t pos, int key,
                       bool edited)>
        after_key;
};

// The newest line entered at a prompt (":" = the last command line), or "".
std::string prompt_history_last(std::string_view prompt);

// Reads a line at the bottom of the screen.
std::string editor_prompt(std::string_view prompt, const PromptHooks& hooks,
                          std::string_view initial = {});

// Older form: `callback` sees every key after the prompt handled it.
std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int),
                          std::string_view initial = {});
