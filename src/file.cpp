#include "file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>

#include "editor.hpp"
#include "prompt.hpp"
#include "terminal.hpp"

void editor_open(const std::string& filename) {
    E.filename = filename;
    std::ifstream in(filename, std::ios::binary);
    if(!in) die("fopen");
    E.orig.assign(std::istreambuf_iterator<char>(in),
                  std::istreambuf_iterator<char>());
    E.eol = E.orig.empty() || E.orig.back() == '\n';
    std::string_view view = E.orig;
    if(!E.orig.empty() && E.orig.back() == '\n') view.remove_suffix(1);
    E.pt = PieceTable(view);
    if(E.syntax.attach(filename)) E.syntax.parse(E.pt, E.eol);
}

namespace {

static bool write_all(int fd, std::string_view s) {
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

}  // namespace

int editor_write_file(const std::string& path) {
    const std::string tmp = path + ".winter.tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd == -1) return errno;
    struct stat st;
    if(stat(path.c_str(), &st) == 0) fchmod(fd, st.st_mode & 07777);
    int err = 0;
    E.pt.for_each_chunk(0, E.pt.size(), [&](std::string_view chunk) {
        if(!err && !write_all(fd, chunk)) err = errno;
    });
    if(!err && E.eol && (E.pt.size() > 0 || !E.orig.empty()) &&
       !write_all(fd, "\n"))
        err = errno;
    if(!err && fsync(fd) == -1) err = errno;
    if(close(fd) == -1 && !err) err = errno;
    if(!err && rename(tmp.c_str(), path.c_str()) == -1) err = errno;
    if(err) unlink(tmp.c_str());
    return err;
}

void editor_save() {
    if(E.filename == "") {
        E.filename = editor_prompt("Save As: ", nullptr);
        if(E.filename == "") {
            editor_set_status_message("Save Aborted");
            return;
        }
    }
    const size_t len = E.pt.size() + (E.eol ? 1 : 0);
    if(const int err = editor_write_file(E.filename)) {
        editor_set_status_message("Can't save! IO error: {}", strerror(err));
        return;
    }
    E.pt.mark_saved();
    editor_set_status_message("{} bytes written to disk", len);
}
