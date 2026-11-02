#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "buffer.hpp"

// Style overrides set from Lua. Every consumer passes its built-in look as
// the default, so with no overrides the editor looks exactly as before.
std::optional<Style> theme_override(std::string_view group);
Style theme_ui(std::string_view group, Style dflt);
uint32_t theme_fg(std::string_view group, uint32_t dflt);  // fg of a group

void theme_set(std::string_view group, const Style& st);
bool theme_unset(std::string_view group);  // true if it existed
void theme_clear();                        // drop every override + rainbow

void theme_set_rainbow(std::vector<uint32_t> colors);
std::span<const uint32_t> theme_rainbow();  // empty = built-in palette

void theme_link(std::string_view group, std::string_view target);
bool theme_unlink(std::string_view group);
const std::vector<std::string_view>& theme_ui_groups();
uint64_t theme_rev();  // bumps on every change
