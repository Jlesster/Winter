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

#include "file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include "columns.hpp"
#include "events.hpp"
#include "external.hpp"
#include "filetype.hpp"
#include "format.hpp"
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

std::string buffer_head(const Buffer& b) {
    return b.pt.text(0, std::min<size_t>(b.pt.size(), 1024));
}

// A language is wanted but has no grammar yet: build it in the background
// (opt ts_autoinstall), once per session per language, also after a failure so
// a missing compiler or tree-sitter CLI doesn't retry on every file open.
static void ensure_parser(std::string_view lang) {
    static std::set<std::string, std::less<>> tried;
    const std::string l(lang);
    if(opts.ts_autoinstall == 0 || !tried.insert(l).second) {
        editor_set_status_message("No {} parser: :TSInstall {}", l, l);
        return;
    }
    const JobId id = parser_install(
        l,
        [l](std::string_view line) {
            editor_set_status_message("TSInstall {}: {}", l, line);
        },
        [l](bool ok, std::string msg) {
            if(!ok) {
                editor_set_status_message("TSInstall {} failed: {}", l, msg);
                return;
            }
            buffer_reattach_syntax(l);
            editor_set_status_message("TSInstall {}: installed", l);
        });
    if(!id) editor_set_status_message("No {} parser: :TSInstall {}", l, l);
}

void buffer_detect_syntax(Buffer& b) {
    if(b.nofile) return;
    const std::string head = buffer_head(b);
    const ft::Resolved r = ft::resolve(b.filename, head);
    if(r.use.empty()) {
        b.syntax.attach({});  // drop a stale tree (renamed file, new filetype)
    } else if(b.syntax.attach_lang(r.use)) {
        b.syntax.parse(b.pt, b.eol);
        EventBufScope scope(&b);
        events_emit("file_type", {b.filename, std::string(b.syntax.lang())});
    }
    // the file is zsh/fish/tmux but only a close grammar (or none) is there
    if(!r.want.empty() && r.want != r.use && parser_known(r.want) &&
       !parser_installed(r.want))
        ensure_parser(r.want);
}

void buffer_reattach_syntax(std::string_view lang) {
    for(auto& b : E.buffers) {
        if(b->nofile) continue;
        const ft::Resolved r = ft::resolve(b->filename, buffer_head(*b));
        if(r.want != lang && r.use != lang) continue;
        if(!r.use.empty() && b->syntax.attach_lang(r.use))
            b->syntax.parse(b->pt, b->eol);
    }
}

bool buffer_set_filetype(Buffer& b, std::string_view lang) {
    if(!b.syntax.attach_lang(lang)) {
        buffer_detect_syntax(b);  // attach_lang detached us: restore
        return false;
    }
    b.syntax.parse(b.pt, b.eol);
    EventBufScope scope(&b);
    events_emit("file_type", {b.filename, std::string(b.syntax.lang())});
    return true;
}

static void restore_cursor(Buffer& b) {
    int line = 0, col = 0;
    if(b.nofile || !cursor_recall(b.filename, line, col)) return;
    b.cy = std::clamp(line, 0, static_cast<int>(b.pt.line_count()) - 1);
    const std::string text = b.pt.line_text(static_cast<size_t>(b.cy));
    const int size = static_cast<int>(text.size());
    int cx = std::clamp(col, 0, size);
    cx = snap_cx(text, cx);
    if(cx >= size && size > 0)
        cx = static_cast<int>(gr_prev(text, text.size()));
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
        b.wire();
        b.disk_hash = state_hash("");
        b.disk_size = 0;
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
    b.disk_hash = state_hash(b.orig);
    b.disk_size = b.orig.size();
    std::string_view view = b.orig;
    if(!b.orig.empty() && b.orig.back() == '\n') view.remove_suffix(1);
    b.pt = PieceTable(view);
    b.wire();
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
       (b.pt.size() > 0 || !b.orig.empty())) {
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
    EventBufScope scope(&b);
    if(opts.format_on_save) format_on_save(b);
    events_emit("buf_write_pre", {b.filename});
    if(const int err = editor_write_file(b, b.filename, &dh, &ds)) return err;
    b.pt.mark_saved();
    b.disk_hash = dh;  // so the fs echo of our own write is a no-op
    b.disk_size = ds;
    b.disk_changed = b.disk_gone = false;
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
    bool named_now = false;
    if(E.buf().filename == "") {
        E.buf().filename = editor_prompt("Save As: ", nullptr);
        if(E.buf().filename == "") {
            editor_set_status_message("Save Aborted");
            return;
        }
        external_watch(E.buf());
        named_now = true;
    }
    if(const int err = write_buffer(E.buf())) {
        editor_set_status_message("Can't save! IO error: {}", strerror(err));
        return;
    }
    if(named_now) {  // a new name is new information: detect, then LSP
        buffer_detect_syntax(E.buf());
        lsp_sync_attach(E.buf());
    }
    const size_t len = E.buf().pt.size() + (E.buf().eol ? 1 : 0);
    editor_set_status_message("{} bytes written to disk{}", len,
                              format_take_note());
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
    msg += format_take_note();
    editor_set_status_message("{}", msg);
}
