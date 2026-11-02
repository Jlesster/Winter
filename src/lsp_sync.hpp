#pragma once

#include <optional>
#include <string>
#include "lsp_client.hpp"

struct Buffer;

struct LspDoc {
    LspId server;
    std::string uri;
    bool utf16;
};

void lsp_sync_attach(Buffer& b);
void lsp_sync_detach(Buffer& b);
void lsp_sync_saved(Buffer& b);
void lsp_sync_tick();
void lsp_sync_flush();
std::optional<LspDoc> lsp_sync_doc(Buffer& b, bool flush_pending = false);
std::string lsp_sync_semantic_report(Buffer& b);
bool lsp_sync_semantic_debug(Buffer& b);
void lsp_sync_semantic_refresh();
