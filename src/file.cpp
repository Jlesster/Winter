#include "file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#include "columns.hpp"
#include "events.hpp"
#include "editor.hpp"
#include "lsp_sync.hpp"
#include "options.hpp"
#include "parser.hpp"
#include "piece.hpp"
#include "prompt.hpp"
#include "state.hpp"
#include "syntax.hpp"
#include "undofile.hpp"
#include "utf8.hpp"

// file io

void buffer_detect_syntax(Buffer& b) {
    if(b.nofile || b.filename.empty()) return;
    if(b.syntax.attach(b.filename)) {
        b.syntax.parse(b.pt, b.eol);
        events_emit("file_type", {b.filename, std::string(b.syntax.lang())});
        return;
    }
    const auto l = Syntax::detect(b.filename);
    if(!l.empty() && parser_known(l) && !parser_installed(l))
        editor_set_status_message("No {} parser: :TSInstall {}", l, l);
}

static void restore_cursor(Buffer& b) {
    int line = 0, col = 0;
    if(b.nofile || !cursor_recall(b.filename, line, col)) return;
    b.cy = std::clamp(line, 0, static_cast<int>(b.pt.line_count()) - 1);
    const std::string text = b.pt.line_text(static_cast<size_t>(b.cy));
    const int size = static_cast<int>(text.size());
    int cx = std::clamp(col, 0, size);
    while(cx > 0 && cx < size && utf8_is_cont(text[cx])) --cx;
    if(cx >= size && size > 0)
        cx = static_cast<int>(utf8_prev(text, text.size()));
    b.cx = cx;
    b.want_rx = cx_to_rx(text, cx);
    b.row_offset = std::max(b.cy - E.term_rows / 2, 0);
}

bool load_file(Buffer& b, const std::string& path) {
    b.filename = path;
    struct stat st;
    if(stat(path.c_str(), &st) != 0) {
        if(errno != ENOENT) return false;
        b.orig.clear();
        b.eol = true;
        b.pt = PieceTable(std::string_view{});
        b.pt.set_history_limit(static_cast<size_t>(opts.undo_levels));
        buffer_detect_syntax(b);
        return true;
    }
    if(S_ISDIR(st.st_mode)) return false;
    std::ifstream in(path, std::ios::binary);
    if(!in) return false;
    b.orig.assign(std::istreambuf_iterator<char>(in),
                  std::istreambuf_iterator<char>());
    b.eol = b.orig.empty() || b.orig.back() == '\n';
    std::string_view view = b.orig;
    if(!b.orig.empty() && b.orig.back() == '\n') view.remove_suffix(1);
    b.pt = PieceTable(view);
    undofile_load(b);
    b.pt.set_history_limit(static_cast<size_t>(opts.undo_levels));
    buffer_detect_syntax(b);
    restore_cursor(b);
    return true;
}

bool write_all(int fd, std::string_view s) {
    while(!s.empty()) {
        const ssize_t n = ::write(fd, s.data(), s.size());
        if(n < 0) {
            if(errno == EINTR) continue;
            return false;
        }
        s.remove_prefix(static_cast<size_t>(n));
    }
    return true;
}

int editor_write_file(Buffer& b, const std::string& path,
                      uint64_t* hash, uint64_t* size) {
    const std::string tmp = path + ".winter.tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd == -1) return errno;
    struct stat st;
    if(stat(path.c_str(), &st) == 0) fchmod(fd, st.st_mode & 07777);
    int err = 0;
    uint64_t h = state_hash(""), n = 0;
    b.pt.for_each_chunk(
        0, b.pt.size(), [&](std::string_view chunk) {
            if(err) return;
            if(!write_all(fd, chunk)) {
                err = errno;
                return;
            }
            h = state_hash(chunk, h);
            n += chunk.size();
        });
    if(!err && b.eol &&
       (b.pt.size() > 0 || !E.buf().orig.empty())) {
        if(write_all(fd, "\n")) {
            h = state_hash("\n", h);
            ++n;
        } else err = errno;
    }
    if(!err && fsync(fd) == -1) err = errno;
    if(close(fd) == -1 && !err) err = errno;
    if(!err && rename(tmp.c_str(), path.c_str()) == -1) err = errno;
    if(err) unlink(tmp.c_str());
    if(!err) {
        if(hash) *hash = h;
        if(size) *size = n;
    }
    return err;
}

static int write_buffer(Buffer& b) {
    uint64_t dh = 0, ds = 0;
    events_emit("buf_write_pre", {b.filename});
    if(const int err = editor_write_file(b, b.filename, &dh, &ds)) return err;
    b.pt.mark_saved();
    undofile_write(b, dh, ds);
    lsp_sync_saved(b);
    events_emit("buf_write_post", {b.filename});
    return 0;
}

void editor_save() {
    if(E.buf().nofile) {
        editor_set_status_message("{} is not a file buf", E.buf().filename);
        return;
    }
    if(E.buf().filename == "") {
        E.buf().filename = editor_prompt("Save As: ", nullptr);
        if(E.buf().filename == "") {
            editor_set_status_message("Save Aborted");
            buffer_detect_syntax(E.buf());
            lsp_sync_attach(E.buf());
            return;
        }
    }
    const size_t len = E.buf().pt.size() + (E.buf().eol ? 1 : 0);
    if(const int err = write_buffer(E.buf())) {
        editor_set_status_message("Can't save! IO error: {}", strerror(err));
        return;
    }
    editor_set_status_message("{} bytes written to disk", len);
}

void editor_save_all() {
    size_t saved = 0, failed = 0, unnamed = 0;
    for(const auto& bp : E.buffers) {
        Buffer& b = *bp;
        if(b.nofile || !b.pt.modified()) continue;
        if(b.filename.empty()) {  // no path to write to, can't prompt per buffer
            if(b.pt.size() > 0) ++unnamed;
            continue;
        }
        if(write_buffer(b)) ++failed;
        else ++saved;
    }
    std::string msg = std::format("{} buffer(s) written", saved);
    if(failed) msg += std::format(", {} failed", failed);
    if(unnamed) msg += std::format(", {} unnamed skipped", unnamed);
    editor_set_status_message("{}", msg);
}
