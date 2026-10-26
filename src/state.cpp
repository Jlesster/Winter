#include "state.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "file.hpp"
#include "options.hpp"

namespace fs = std::filesystem;

std::string state_dir() {
    static const std::string dir = [] {
        std::string base;
        if(const char* x = std::getenv("XDG_STATE_HOME"); x && *x) base = x;
        else if(const char* h = std::getenv("HOME"); h && *h)
            base = std::string(h) + "/.local/state";
        else return std::string{};
        const std::string d = base + "/winter";
        std::error_code ec;
        fs::create_directories(d, ec);
        if(ec) return std::string{};
        chmod(d.c_str(), 0700);
        return d;
    }();
    return dir;
}

namespace {
std::string path_of(std::string_view name) {
    const std::string d = state_dir();
    return d.empty() ? std::string{} : d + "/" + std::string(name);
}
bool to_int(std::string_view s, int& v) {
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc{} && p == s.data() + s.size();
}
}  // namespace

bool state_append(std::string_view name, std::string_view line) {
    const std::string path = path_of(name);
    if(path.empty()) return false;
    std::string data(line);
    data += '\n';
    // fresh open per append + O_APPEND: safe with several winters at once
    const int fd =
        open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if(fd == -1) return false;
    const bool ok = write_all(fd, data);
    close(fd);
    return ok;
}

bool state_write_blob(std::string_view name, std::string_view data) {
    const std::string path = path_of(name);
    if(path.empty()) return false;
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    fs::create_directories(parent, ec);
    chmod(parent.c_str(), 0700);
    const std::string tmp = path + ".tmp" + std::to_string(getpid());
    const int fd =
        open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if(fd == -1) return false;
    bool ok = write_all(fd, data);
    ok = (close(fd) == 0) && ok;
    if(!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool state_rewrite(std::string_view name,
                   const std::vector<std::string>& lines) {
    std::string data;
    for(const std::string& l : lines) (data += l) += '\n';
    return state_write_blob(name, data);
}

std::optional<std::string> state_read_blob(std::string_view name) {
    const std::string path = path_of(name);
    if(path.empty()) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if(!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void state_remove(std::string_view name) {
    const std::string path = path_of(name);
    if(!path.empty()) unlink(path.c_str());
}

std::vector<std::string> state_read_lines(std::string_view name) {
    std::vector<std::string> out;
    const std::string path = path_of(name);
    if(path.empty()) return out;
    std::ifstream in(path, std::ios::binary);
    for(std::string l; std::getline(in, l);)
        if(!l.empty()) out.push_back(std::move(l));
    return out;
}

uint64_t state_hash(std::string_view s, uint64_t h) {
    for(const unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

HistoryMap state_history_load() {
    HistoryMap h;
    const auto lines = state_read_lines("history");
    for(const std::string& l : lines) {
        const size_t tab = l.find('\t');
        if(tab == std::string::npos) continue;
        auto& v = h[l.substr(0, tab)];
        const std::string e = l.substr(tab + 1);
        std::erase(v, e);  // newest copy wins
        v.push_back(e);
    }
    const size_t cap = static_cast<size_t>(std::max(opts.history, 0));
    size_t kept = 0;
    for(auto& [k, v] : h) {
        if(v.size() > cap)
            v.erase(v.begin(),
                    v.begin() + static_cast<std::ptrdiff_t>(v.size() - cap));
        kept += v.size();
    }
    if(lines.size() > 2 * kept + 64) {  // log bloated: compact it
        std::vector<std::string> out;
        for(const auto& [k, v] : h)
            for(const std::string& e : v) out.push_back(k + '\t' + e);
        state_rewrite("history", out);
    }
    return h;
}

void state_history_append(std::string_view prompt, std::string_view entry) {
    if(opts.history <= 0 || entry.find('\n') != std::string_view::npos) return;
    state_append("history", std::string(prompt) + '\t' + std::string(entry));
}

std::string state_file_key(const std::string& file) {
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(file, ec);
    if(!ec) return p.string();
    return fs::absolute(file, ec).lexically_normal().string();
}

namespace {
struct Pos {
    int line, col;
};

std::map<std::string, Pos>& cursors() {
    static std::map<std::string, Pos> m = [] {
        std::map<std::string, Pos> out;
        const auto lines = state_read_lines("cursor");
        for(const std::string& l : lines) {
            const size_t b =
                l.rfind('\t');  // split from the right: paths may hold tabs
            if(b == std::string::npos || b == 0) continue;
            const size_t a = l.rfind('\t', b - 1);
            if(a == std::string::npos) continue;
            Pos p{};
            if(!to_int(std::string_view(l).substr(a + 1, b - a - 1), p.line) ||
               !to_int(std::string_view(l).substr(b + 1), p.col))
                continue;
            out[l.substr(0, a)] = p;
        }
        if(lines.size() > 2 * out.size() + 64) {
            std::vector<std::string> v;
            for(const auto& [k, p] : out)
                v.push_back(k + '\t' + std::to_string(p.line) + '\t' +
                            std::to_string(p.col));
            state_rewrite("cursor", v);
        }
        return out;
    }();
    return m;
}
}  // namespace

void cursor_remember(const std::string& file, int line, int col) {
    if(!opts.remember_cursor || file.empty()) return;
    const std::string key = state_file_key(file);
    if(key.find('\n') != std::string::npos) return;
    auto& m = cursors();
    if(const auto it = m.find(key);
       it != m.end() && it->second.line == line && it->second.col == col)
        return;  // unchanged: don't grow the log
    m[key] = {line, col};
    state_append(
        "cursor",
        key + '\t' + std::to_string(line) + '\t' + std::to_string(col));
}

bool cursor_recall(const std::string& file, int& line, int& col) {
    if(!opts.remember_cursor || file.empty()) return false;
    const auto& m = cursors();
    const auto it = m.find(state_file_key(file));
    if(it == m.end()) return false;
    line = it->second.line;
    col = it->second.col;
    return true;
}
