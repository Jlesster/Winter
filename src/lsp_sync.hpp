#pragma once

#include <optional>
#include <string>
#include <string_view>
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
std::string lsp_sync_semantic_report(Buffer& b);
bool lsp_sync_semantic_debug(Buffer& b);
void lsp_sync_semantic_refresh();

std::string lsp_sync_server_of(Buffer& b);
std::string lsp_sync_server_start(std::string_view name);
std::string lsp_sync_server_stop(std::string_view name);
std::string lsp_sync_server_restart(std::string_view name);
std::string lsp_sync_server_report();
