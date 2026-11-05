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

// Reads a line at the bottom of the screen.
std::string editor_prompt(std::string_view prompt, const PromptHooks& hooks,
                          std::string_view initial = {});

// Older form: `callback` sees every key after the prompt handled it.
std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int),
                          std::string_view initial = {});
