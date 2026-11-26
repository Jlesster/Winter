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
#include <optional>
#include <string>
#include <string_view>
#include "options.hpp"
#include "overlay.hpp"

struct InputSpec {
    std::string prompt;
    std::string initial;
    int w = 40;
    OverlaySpec::At at = OverlaySpec::At::Center;
    int x = 0, y = 0;
    std::optional<Border> border;
    Style style{};
    Style prompt_style{.attrs = ATTR_BOLD};
    std::function<void(std::string_view)> on_change;
    std::function<void(std::string)> on_submit;
    std::function<void()> on_cancel;
    std::function<bool(int)> on_key;
    Style border_style{};      // frame color; falls back to ui.input.border
    std::string title;         // shown in the top border
    bool join_below = false;   // bottom border becomes a divider (box below)
};

OverlayId input_open(InputSpec);
