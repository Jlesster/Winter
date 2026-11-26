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

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "lsp_client.hpp"

struct Buffer;

struct LspDoc {
    LspId server;
    std::string uri;
    bool utf16;
    std::string server_name;
};

void lsp_sync_attach(Buffer& b);
void lsp_sync_detach(Buffer& b);
std::optional<int> lsp_sync_version(Buffer& b);
void lsp_sync_saved(Buffer& b);
void lsp_sync_tick();
void lsp_sync_flush();
std::optional<LspDoc> lsp_sync_doc(Buffer& b, bool flush_pending = false);
// Show the server a copy of the document without these [start, end) byte
// ranges (ascending, not overlapping), for a request that must not see them.
// The real text goes back on the next lsp_sync_doc(b, true) / flush. Returns
// the document like lsp_sync_doc; the ranges are not masked if they are not
// valid or the server takes no changes (lsp_sync_masked says which).
std::optional<LspDoc> lsp_sync_mask(
    Buffer& b, const std::vector<std::pair<size_t, size_t>>& ranges);
bool lsp_sync_masked(Buffer& b);
std::string lsp_sync_semantic_report(Buffer& b);
bool lsp_sync_semantic_debug(Buffer& b);
void lsp_sync_semantic_refresh();

// What the servers last published, whether or not the file is open. Positions
// are as the server sent them (character in utf16 units unless utf16 is false).
// Open buffers are better read from Buffer::diags: those follow edits.
struct PublishedDiag {
    std::string path;  // absolute, as lsp_path gives it
    int line = 0, character = 0;
    bool utf16 = true;
    int severity = 1;
    std::string msg, source;
};
std::vector<PublishedDiag> lsp_sync_published();

std::string lsp_sync_server_of(Buffer& b);
std::string lsp_sync_server_start(std::string_view name);
std::string lsp_sync_server_stop(std::string_view name);
std::string lsp_sync_server_restart(std::string_view name);
std::string lsp_sync_server_report();
