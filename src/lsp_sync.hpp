#pragma once

struct Buffer;

void lsp_sync_attach(Buffer& b);
void lsp_sync_detach(Buffer& b);
void lsp_sync_saved(Buffer& b);
void lsp_sync_tick();
void lsp_sync_flush();
