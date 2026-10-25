#pragma once

#include <string>
#include <string_view>

struct PromptView {
    bool active = false;
    int col = 0;
};
PromptView& prompt_view();

// Reads a line at the bottom of the screen. `callback` sees every key.
std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int),
                          std::string_view initial = {});
