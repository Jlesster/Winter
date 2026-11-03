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
