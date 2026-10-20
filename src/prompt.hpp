#pragma once

#include <string>
#include <string_view>

// Reads a line at the bottom of the screen. `callback` sees every key.
std::string editor_prompt(std::string_view prompt,
                          void (*callback)(std::string, int));
