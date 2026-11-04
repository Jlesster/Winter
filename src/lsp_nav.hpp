#pragma once
#include <string_view>
#include <string>
#include <cstddef>

enum class NavKind {
    Definition,
    Declaration,
    TypeDefinition,
    Implementation,
    References,
};

bool nav_kind_from_name(std::string_view name, NavKind& out);

void lsp_goto(NavKind kind);
void lsp_switch_source_header();
void lsp_nav_register_commands();

void lsp_show_location(const std::string& path,
                       size_t line, size_t ch, bool utf16);
