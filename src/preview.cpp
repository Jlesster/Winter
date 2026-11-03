#include "preview.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string_view>
#include <vector>

#include "columns.hpp"
#include "theme.hpp"
#include "utf8.hpp"
#include "virt.hpp"

namespace {

constexpr size_t kMaxRead = 2u << 20;    // never read more than 2 MiB
constexpr size_t kMaxSyntax = 512u << 10;  // no tree-sitter above 512 KiB
constexpr size_t kMaxLineBytes = 2000;   // longer lines are cut when drawn

}  // namespace

void FilePreview::clear() {
    path_.clear();
    text_.clear();
    msg_.clear();
    pt_ = PieceTable(std::string_view{});
    syn_.attach({});  // no language: drops the old tree and queries
    fl_ = fc_ = 0;
}

void FilePreview::load(const std::string& path) {
    clear();
    path_ = path;

    struct stat st;
    if(stat(path.c_str(), &st) != 0) {
        msg_ = "cannot open file";
        return;
    }
    if(S_ISDIR(st.st_mode)) {
        msg_ = "directory";
        return;
    }
    if(!S_ISREG(st.st_mode)) {  // a fifo would block the whole editor
        msg_ = "not a regular file";
        return;
    }
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if(fd == -1) {
        msg_ = "cannot open file";
        return;
    }
    char buf[65536];
    while(text_.size() < kMaxRead) {
        const ssize_t n =
            read(fd, buf, std::min(sizeof buf, kMaxRead - text_.size()));
            if(n > 0) {
                text_.append(buf, static_cast<size_t>(n));
                continue;
            }
            if(n < 0 && errno == EINTR) continue;
            break;
    }
    close(fd);

    if(std::memchr(text_.data(), '\0', std::min<size_t>(text_.size(), 8192))) {
        text_.clear();
        msg_ = "binary file";
        return;
    }
    const bool truncated = static_cast<size_t>(st.st_size) > text_.size();
    eol_ = text_.empty() || text_.back() == '\n';
    std::string_view view = text_;
    if(!text_.empty() && text_.back() == '\n') view.remove_suffix(1);
    pt_ = PieceTable(view);
    if(!truncated && text_.size() <= kMaxSyntax && syn_.attach(path))
        syn_.parse(pt_, eol_);
}

void FilePreview::paint(OverlayId id, int w, int h, const std::string& want,
                        int focus_line, int focus_col) {
    overlay_clear(id);
    const int iw = w - 2, ih = h - 2;  // inside the frame
    if(iw < 8 || ih < 1 || path_.empty()) return;
    const Style dim = theme_ui("ui.linenr", Style{.attrs = ATTR_DIM});
    if(!msg_.empty()) {
        overlay_put(id, 2, 1, msg_, dim);
        return;
    }
    if(want == path_) fl_ = focus_line, fc_ = focus_col;

    const int lines = static_cast<int>(pt_.line_count());
    const int digits = static_cast<int>(std::to_string(lines).size());
    const int gutter = std::max(3, digits) + 1;
    const int cols = iw - gutter;
    if(cols < 4) return;

    // Vertical: centre the focus line, but don't scroll past the end.
    int top = 0;
    if(fl_ > 0 && lines > ih)
        top = std::clamp(fl_ - 1 - ih / 2, 0, lines - ih);

    // Horizontal: only move when the match would be off screen.
    int hscroll = 0;
    if(fl_ > 0 && fc_ > 0 && fl_ <= lines) {
        const std::string fl = pt_.line_text(static_cast<size_t>(fl_ - 1));
        const int cx = std::min<int>(fc_ - 1, static_cast<int>(fl.size()));
        const int rx = cx_to_rx(fl, cx);
        if(rx >= cols - 8) hscroll = rx - cols / 3;
    }

    const Style cur_st = theme_ui("ui.cursorline", Style{.bg = 0x313244});
    const Style cur_nr =
        theme_ui("ui.linenr.cursor", Style{.attrs = ATTR_BOLD});
        const Style match_st =
            theme_ui("ui.menu.match", Style{.attrs = ATTR_BOLD | ATTR_UNDERLINE});

            for(int r = 0; r < ih && top + r < lines; ++r) {
                const int l = top + r;
                const bool focus = l + 1 == fl_;
                const int y = 1 + r;
                Style row{};
                if(focus) {
                    row = cur_st;
                    overlay_put(id, 1, y, std::string(static_cast<size_t>(iw), ' '),
                                row);
                }
                Style nr = focus ? cur_nr : dim;
                if(focus) style_patch(nr, cur_st);
                std::string num = std::to_string(l + 1);
                if(static_cast<int>(num.size()) < gutter - 1)
                    num.insert(0, static_cast<size_t>(gutter - 1) - num.size(), ' ');
                overlay_put(id, 1, y, num, nr);

                const std::string text = pt_.line_text(static_cast<size_t>(l));
                const std::vector<Span>& spans =
                    syn_.line_spans(pt_.line_start(static_cast<size_t>(l)), text.size());
                    const size_t cap = std::min(text.size(), kMaxLineBytes);
                    const size_t mark = focus && fc_ > 0 ? static_cast<size_t>(fc_ - 1)
                        : static_cast<size_t>(-1);
                        size_t si = 0;
                        int rx = 0;
                        for(size_t i = 0; i < cap;) {
                            const Decoded d = utf8_decode(text, i);
                            const int cw = cell_width(d.cp, rx);
                            Style st = row;
                            while(si < spans.size() && spans[si].end <= i) ++si;
                            for(size_t k = si; k < spans.size() && spans[k].start <= i; ++k)
                                if(i < spans[k].end) style_patch(st, spans[k].st);
                            if(i == mark) style_patch(st, match_st);

                            const int vx = rx - hscroll;  // column inside the text area
                            rx += cw;
                            i += d.len;
                            if(vx + cw <= 0) continue;  // scrolled out to the left
                            if(vx < 0 || vx + cw > cols) {
                                if(vx >= 0) break;  // would not fit: stop at the right edge
                                continue;           // half cut at the left edge
                            }
                            std::string cell;
                            if(d.cp == '\t') cell.assign(static_cast<size_t>(cw), ' ');
                            else if(is_ctl(d.cp)) cell = "?";
                            else cell.assign(text, i - d.len, d.len);
                            overlay_put(id, 1 + gutter + vx, y, cell, st);
                        }
            }
}
