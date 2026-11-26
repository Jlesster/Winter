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
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"

using LspId = int;
using LspReply = std::function<void(const Json& result, const Json& error)>;
using LspNotify = std::function<void(LspId, const Json& params)>;
using LspRequestFn = std::function<Json(LspId, const Json& params)>;
using LspRespond = std::function<void(Json)>;
using LspAsyncRequestFn =
    std::function<void(LspId, const Json& params, LspRespond)>;

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

// Returns the request id (0 if nothing was sent: the server is not running,
// in which case cb has already been called with an error).
int lsp_request(LspId id, std::string_view method, Json params,
                LspReply cb = {});
// Forget the reply to request `rid` and tell the server ($/cancelRequest).
void lsp_cancel(LspId id, int rid);
void lsp_notify(LspId id, std::string_view method, Json params = {});

void lsp_on(std::string_view method, LspNotify fn);
void lsp_on_request(std::string_view method, LspRequestFn fn);
void lsp_on_request_async(std::string_view method, LspAsyncRequestFn fn);

bool lsp_ready(LspId id);
const Json& lsp_capabilities(LspId id);
std::string_view lsp_encoding(LspId id);
std::string_view lsp_stderr(LspId id);

std::string lsp_uri(std::string_view path);
std::string lsp_path(std::string_view uri);
