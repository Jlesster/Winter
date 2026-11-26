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

#include <string>

#include "overlay.hpp"
#include "piece.hpp"
#include "syntax.hpp"

class FilePreview {
public:
    FilePreview() = default;
    FilePreview(const FilePreview&) = delete;
    FilePreview& operator=(const FilePreview&) = delete;

    void load(const std::string& path);
    void clear();
    const std::string& path() const { return path_; }

    void paint(OverlayId id, int w, int h, const std::string& want,
               int focus_line, int focus_col);

private:
    std::string path_;
    std::string text_;
    std::string msg_;
    PieceTable pt_;
    Syntax syn_;
    bool eol_ = true;
    int fl_ = 0, fc_ = 0;
};
