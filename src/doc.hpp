#pragma once
// :Doc, :DocInstall, :DocUpdate: offline documentation (see doc_core.hpp for
// the editor-independent half and the on-disk layout).

void doc_register_commands();

// K in the doc split: follow the markdown link under the cursor. Returns
// false when the current buffer is not the doc split, so the caller falls
// through to the LSP hover.
bool doc_follow_link();
