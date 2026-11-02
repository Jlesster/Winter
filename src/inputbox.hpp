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
};

OverlayId input_open(InputSpec);
