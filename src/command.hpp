#pragma once

#include <string_view>

inline constexpr std::string_view kVisualRange = "V/>>";

void editor_command_palette(std::string_view cmd);
void editor_command_prompt();
void register_builtin_commands();
