#pragma once
#include <string>
#include "json.hpp"
#include "lsp_client.hpp"

struct ApplyResult {
    bool ok = true;
    std::string error;
    size_t files = 0, edits = 0;
};

ApplyResult lsp_apply_workspace_edit(LspId server, const Json& edit);
