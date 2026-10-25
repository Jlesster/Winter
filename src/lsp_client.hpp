#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"

using LspId = int;
using LspReply = std::function<void(const Json& result, const Json& error)>;
using LspNotify = std::function<void(LspId, const Json& params)>;

struct LspSpec {
    std::string name;
    std::string root;
    std::vector<std::string> cmd;
    Json init_options;
    Json settings;
    std::function<void(LspId)> on_ready;
    std::function<void(LspId, std::string_view)> on_exit;
};

LspId lsp_start(LspSpec spec);
void lsp_stop(LspId id);

void lsp_request(LspId id, std::string_view method, Json params,
                 LspReply cb = {});
void lsp_notify(LspId id, std::string_view method, Json params = {});

void lsp_on(std::string_view method, LspNotify fn);

bool lsp_ready(LspId id);
const Json& lsp_capabilities(LspId id);
std::string_view lsp_encoding(LspId id);
std::string_view lsp_stderr(LspId id);

std::string lsp_uri(std::string_view path);
std::string lsp_path(std::string_view uri);
