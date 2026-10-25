#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"

std::string lsp_configure(std::string_view name, Json opts);
bool lsp_enabled(std::string_view name);
std::vector<std::string> lsp_enabled_names();

Json lsp_resolve(std::string_view name, const Json& builtin);
