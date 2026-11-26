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
// :Doc, :DocInstall, :DocUpdate: offline documentation (see doc_core.hpp for
// the editor-independent half and the on-disk layout).

void doc_register_commands();

// K in the doc split: follow the markdown link under the cursor. Returns
// false when the current buffer is not the doc split, so the caller falls
// through to the LSP hover.
bool doc_follow_link();
