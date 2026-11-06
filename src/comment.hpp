#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

struct CommentCont {
    std::string prefix;
    bool lone;
};

struct CommentStyle {
    std::string line, open, close;
};

void comment_register(std::string lang, CommentStyle s);
const CommentStyle* comment_style(std::string_view lang);
void comment_toggle(size_t l0, size_t l1);
std::optional<CommentCont> comment_continue(size_t line, size_t cx, bool above);
