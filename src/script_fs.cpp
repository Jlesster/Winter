// winter.fs: files and paths for plugins. Everything returns nil, err on
// failure instead of raising, so a plugin can probe freely.
#include <fnmatch.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <lua.hpp>
#include <string>
#include <vector>

#include "script.hpp"
#include "script_internal.hpp"

namespace fs = std::filesystem;

namespace {

int push_err(lua_State* L, const std::string& msg) {
    lua_pushnil(L);
    lua_pushlstring(L, msg.data(), msg.size());
    return 2;
}

int push_ec(lua_State* L, const std::error_code& ec, const std::string& what) {
    return push_err(L, what + ": " + ec.message());
}

std::string home() {
    const char* h = std::getenv("HOME");
    return h && *h ? h : "";
}

std::string env_dir(const char* var, const char* fallback_sub) {
    if(const char* x = std::getenv(var); x && *x)
        return (fs::path(x) / "winter").string();
    const std::string h = home();
    return h.empty() ? std::string{} : (fs::path(h) / fallback_sub / "winter").string();
}

const char* type_name(fs::file_type t) {
    switch(t) {
    case fs::file_type::regular: return "file";
    case fs::file_type::directory: return "directory";
    case fs::file_type::symlink: return "link";
    default: return "other";
    }
}

// winter.fs.stdpath("config" | "data" | "cache" | "state") -> dir (may not exist)
int l_stdpath(lua_State* L) {
    const std::string k = luaL_checkstring(L, 1);
    std::string d;
    if(k == "config") d = script_config_dir();
    else if(k == "data") d = env_dir("XDG_DATA_HOME", ".local/share");
    else if(k == "cache") d = env_dir("XDG_CACHE_HOME", ".cache");
    else if(k == "state") d = env_dir("XDG_STATE_HOME", ".local/state");
    else return luaL_argerror(L, 1, "config, data, cache or state");
    lua_pushlstring(L, d.data(), d.size());
    return 1;
}

int l_exists(lua_State* L) {
    std::error_code ec;
    lua_pushboolean(L, fs::exists(luaL_checkstring(L, 1), ec));
    return 1;
}

int l_is_dir(lua_State* L) {
    std::error_code ec;
    lua_pushboolean(L, fs::is_directory(luaL_checkstring(L, 1), ec));
    return 1;
}

int l_is_file(lua_State* L) {
    std::error_code ec;
    lua_pushboolean(L, fs::is_regular_file(luaL_checkstring(L, 1), ec));
    return 1;
}

int l_executable(lua_State* L) {
    lua_pushboolean(L, ::access(luaL_checkstring(L, 1), X_OK) == 0);
    return 1;
}

// winter.fs.stat(path) -> {type=, size=, mtime=, executable=} | nil, err
// type: "file" | "directory" | "link" | "other"; links are not followed.
int l_stat(lua_State* L) {
    const std::string p = luaL_checkstring(L, 1);
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(p, ec);
    if(ec || st.type() == fs::file_type::not_found)
        return push_err(L, p + ": no such file or directory");
    lua_createtable(L, 0, 4);
    lua_pushstring(L, type_name(st.type()));
    lua_setfield(L, -2, "type");
    if(st.type() == fs::file_type::regular) {
        const auto sz = fs::file_size(p, ec);
        lua_pushinteger(L, ec ? 0 : static_cast<lua_Integer>(sz));
        lua_setfield(L, -2, "size");
    }
    const auto mt = fs::last_write_time(p, ec);
    if(!ec) {
        const auto sys = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::file_clock::to_sys(mt));
        lua_pushinteger(L, static_cast<lua_Integer>(sys.time_since_epoch().count()));
        lua_setfield(L, -2, "mtime");
    }
    lua_pushboolean(L, ::access(p.c_str(), X_OK) == 0);
    lua_setfield(L, -2, "executable");
    return 1;
}

// winter.fs.read(path) -> string | nil, err
int l_read(lua_State* L) {
    const std::string p = luaL_checkstring(L, 1);
    std::ifstream in(p, std::ios::binary);
    if(!in) return push_err(L, p + ": can't open");
    const std::string s{std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>()};
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// winter.fs.write(path, data [, {append=false, parents=false}]) -> true | nil, err
int l_write(lua_State* L) {
    const std::string p = luaL_checkstring(L, 1);
    size_t n;
    const char* d = luaL_checklstring(L, 2, &n);
    const int t = lua_table_arg(L, 3);
    const bool append = lua_opt_bool(L, t, "append", false);
    const bool parents = lua_opt_bool(L, t, "parents", false);
    if(parents) {
        std::error_code ec;
        const fs::path dir = fs::path(p).parent_path();
        if(!dir.empty()) fs::create_directories(dir, ec);
        if(ec) return push_ec(L, ec, dir.string());
    }
    std::ofstream out(p, std::ios::binary |
                             (append ? std::ios::app : std::ios::trunc));
    if(!out) return push_err(L, p + ": can't open for writing");
    out.write(d, static_cast<std::streamsize>(n));
    out.close();
    if(out.fail()) return push_err(L, p + ": write failed");
    lua_pushboolean(L, 1);
    return 1;
}

// winter.fs.readdir(path) -> { {name=, type=}, ... } sorted by name | nil, err
int l_readdir(lua_State* L) {
    const std::string p = luaL_checkstring(L, 1);
    std::error_code ec;
    std::vector<std::pair<std::string, fs::file_type>> ents;
    for(fs::directory_iterator it(p, ec), end; !ec && it != end;
        it.increment(ec)) {
        std::error_code e2;
        ents.emplace_back(it->path().filename().string(),
                          it->symlink_status(e2).type());
    }
    if(ec) return push_ec(L, ec, p);
    std::ranges::sort(ents, {}, &std::pair<std::string, fs::file_type>::first);
    lua_createtable(L, static_cast<int>(ents.size()), 0);
    lua_Integer i = 1;
    for(const auto& [name, type] : ents) {
        lua_createtable(L, 0, 2);
        lua_pushlstring(L, name.data(), name.size());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, type_name(type));
        lua_setfield(L, -2, "type");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// winter.fs.mkdir(path [, {parents=false}]) -> true | nil, err
// An existing directory is success.
int l_mkdir(lua_State* L) {
    const std::string p = luaL_checkstring(L, 1);
    const int t = lua_table_arg(L, 2);
    std::error_code ec;
    if(fs::is_directory(p, ec)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    if(lua_opt_bool(L, t, "parents", false)) fs::create_directories(p, ec);
    else fs::create_directory(p, ec);
    if(ec) return push_ec(L, ec, p);
    lua_pushboolean(L, 1);
    return 1;
}

// winter.fs.remove(path [, {recursive=false}]) -> true | nil, err
int l_remove(lua_State* L) {
    const std::string p = luaL_checkstring(L, 1);
    const int t = lua_table_arg(L, 2);
    std::error_code ec;
    if(lua_opt_bool(L, t, "recursive", false)) fs::remove_all(p, ec);
    else fs::remove(p, ec);
    if(ec) return push_ec(L, ec, p);
    lua_pushboolean(L, 1);
    return 1;
}

int l_rename(lua_State* L) {
    const std::string a = luaL_checkstring(L, 1), b = luaL_checkstring(L, 2);
    std::error_code ec;
    fs::rename(a, b, ec);
    if(ec) return push_ec(L, ec, a);
    lua_pushboolean(L, 1);
    return 1;
}

int l_realpath(lua_State* L) {
    std::error_code ec;
    const fs::path r = fs::canonical(luaL_checkstring(L, 1), ec);
    if(ec) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, r.c_str());
    return 1;
}

int l_cwd(lua_State* L) {
    std::error_code ec;
    lua_pushstring(L, fs::current_path(ec).c_str());
    return 1;
}

// "a", "b/", "/c" -> "a/b/c"; empty parts are skipped
std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for(const std::string& p : parts) {
        if(p.empty()) continue;
        if(out.empty()) {
            out = p;
            continue;
        }
        const bool a = out.back() == '/', b = p.front() == '/';
        if(a && b) out += p.substr(1);
        else if(a || b) out += p;
        else out += "/" + p;
    }
    return out;
}

int l_joinpath(lua_State* L) {
    std::vector<std::string> parts;
    for(int i = 1, n = lua_gettop(L); i <= n; ++i)
        parts.emplace_back(luaL_checkstring(L, i));
    const std::string r = join(parts);
    lua_pushlstring(L, r.data(), r.size());
    return 1;
}

std::string dirname_of(std::string p) {
    while(p.size() > 1 && p.back() == '/') p.pop_back();
    const size_t s = p.rfind('/');
    if(s == std::string::npos) return ".";
    if(s == 0) return "/";
    return p.substr(0, s);
}

int l_dirname(lua_State* L) {
    const std::string r = dirname_of(luaL_checkstring(L, 1));
    lua_pushlstring(L, r.data(), r.size());
    return 1;
}

int l_basename(lua_State* L) {
    std::string p = luaL_checkstring(L, 1);
    while(p.size() > 1 && p.back() == '/') p.pop_back();
    const size_t s = p.rfind('/');
    if(s != std::string::npos && p.size() > 1) p = p.substr(s + 1);
    lua_pushlstring(L, p.data(), p.size());
    return 1;
}

// winter.fs.normalize(path): expands a leading ~, collapses . .. and //,
// drops a trailing slash. Purely lexical: the path need not exist.
int l_normalize(lua_State* L) {
    std::string p = luaL_checkstring(L, 1);
    if(p == "~" || p.starts_with("~/")) p = home() + p.substr(1);
    std::string r = fs::path(p).lexically_normal().string();
    while(r.size() > 1 && r.back() == '/') r.pop_back();
    if(r.empty()) r = ".";
    lua_pushlstring(L, r.data(), r.size());
    return 1;
}

// ---- find / root -------------------------------------------------------------

bool has_glob(const std::string& s) {
    return s.find_first_of("*?[") != std::string::npos;
}

bool name_hit(const std::vector<std::string>& pats, const std::string& name) {
    for(const std::string& p : pats)
        if(has_glob(p) ? fnmatch(p.c_str(), name.c_str(), 0) == 0 : p == name)
            return true;
    return false;
}

bool type_ok(int want, const fs::path& p) {  // 0 any, 1 file, 2 directory
    if(want == 0) return true;
    std::error_code ec;
    return want == 1 ? fs::is_regular_file(p, ec) : fs::is_directory(p, ec);
}

// names: string | {string...}, entries match by name or glob.
bool read_names(lua_State* L, int idx, std::vector<std::string>& out) {
    idx = lua_absindex(L, idx);
    if(lua_type(L, idx) == LUA_TSTRING) {
        out.emplace_back(lua_tostring(L, idx));
        return true;
    }
    if(!lua_istable(L, idx)) return false;
    const lua_Unsigned n = lua_rawlen(L, idx);
    for(lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
        if(lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 1);
            return false;
        }
        out.emplace_back(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return !out.empty();
}

struct FindOpts {
    std::vector<std::string> names;
    fs::path start, stop;
    bool upward = false;
    int type = 0;
    size_t limit = 1;
};

std::vector<std::string> do_find(const FindOpts& o) {
    std::vector<std::string> out;
    std::error_code ec;
    if(o.upward) {
        fs::path dir = o.start;
        if(!fs::is_directory(dir, ec)) dir = dir.parent_path();
        for(int guard = 0; guard < 4096 && !dir.empty(); ++guard) {
            for(fs::directory_iterator it(dir, ec), end; !ec && it != end;
                it.increment(ec)) {
                const std::string n = it->path().filename().string();
                if(name_hit(o.names, n) && type_ok(o.type, it->path())) {
                    out.push_back(it->path().string());
                    if(out.size() >= o.limit) return out;
                }
            }
            ec.clear();
            if(dir == o.stop || dir == dir.root_path()) break;
            const fs::path up = dir.parent_path();
            if(up == dir) break;
            dir = up;
        }
        return out;
    }
    fs::recursive_directory_iterator it(
        o.start, fs::directory_options::skip_permission_denied, ec);
    for(const fs::recursive_directory_iterator end; !ec && it != end;
        it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if(it->is_directory(ec) && (n == ".git" || n == "node_modules")) {
            it.disable_recursion_pending();
            continue;
        }
        if(name_hit(o.names, n) && type_ok(o.type, it->path())) {
            out.push_back(it->path().string());
            if(out.size() >= o.limit) break;
        }
    }
    return out;
}

// winter.fs.find(names [, {path=cwd, upward=false, type="file"|"directory",
//                          limit=1, stop=dir}]) -> { path... }
// names is a name, a glob ("*.rs") or a list of either. Downward searches skip
// .git and node_modules. upward walks from path (or its directory) to / or
// `stop`, all matches of one directory before the next.
int l_find(lua_State* L) {
    FindOpts o;
    if(!read_names(L, 1, o.names))
        return luaL_argerror(L, 1, "a name or a non-empty list of names");
    const int t = lua_table_arg(L, 2);
    std::error_code ec;
    o.start = fs::current_path(ec);
    o.upward = lua_opt_bool(L, t, "upward", false);
    if(t) {
        lua_getfield(L, t, "path");
        if(lua_type(L, -1) == LUA_TSTRING) o.start = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, t, "stop");
        if(lua_type(L, -1) == LUA_TSTRING) o.stop = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, t, "type");
        if(lua_type(L, -1) == LUA_TSTRING) {
            const std::string ty = lua_tostring(L, -1);
            o.type = ty == "file" ? 1 : ty == "directory" ? 2 : 0;
        }
        lua_pop(L, 1);
        lua_getfield(L, t, "limit");
        if(lua_isnumber(L, -1)) {
            const lua_Number n = lua_tonumber(L, -1);
            o.limit = n < 1 ? 1 : n > 1e9 ? static_cast<size_t>(1e9)
                                          : static_cast<size_t>(n);
        }
        lua_pop(L, 1);
    }
    const std::vector<std::string> r = do_find(o);
    lua_createtable(L, static_cast<int>(r.size()), 0);
    lua_Integer i = 1;
    for(const std::string& s : r) {
        lua_pushlstring(L, s.data(), s.size());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// winter.fs.root(markers [, path]) -> dir | nil: the nearest ancestor of path
// (default: the cwd) that contains one of the markers, e.g.
// root({".git", "Cargo.toml", "*.nimble"}, winter.buf.name()).
int l_root(lua_State* L) {
    FindOpts o;
    if(!read_names(L, 1, o.names))
        return luaL_argerror(L, 1, "a name or a non-empty list of names");
    std::error_code ec;
    o.start = lua_type(L, 2) == LUA_TSTRING ? fs::path(lua_tostring(L, 2))
                                            : fs::current_path(ec);
    o.upward = true;
    const std::vector<std::string> r = do_find(o);
    if(r.empty()) {
        lua_pushnil(L);
        return 1;
    }
    const std::string d = fs::path(r.front()).parent_path().string();
    lua_pushlstring(L, d.data(), d.size());
    return 1;
}

const luaL_Reg kFs[] = {{"stdpath", l_stdpath},
                        {"exists", l_exists},
                        {"is_dir", l_is_dir},
                        {"is_file", l_is_file},
                        {"executable", l_executable},
                        {"stat", l_stat},
                        {"read", l_read},
                        {"write", l_write},
                        {"readdir", l_readdir},
                        {"mkdir", l_mkdir},
                        {"remove", l_remove},
                        {"rename", l_rename},
                        {"realpath", l_realpath},
                        {"cwd", l_cwd},
                        {"joinpath", l_joinpath},
                        {"dirname", l_dirname},
                        {"basename", l_basename},
                        {"normalize", l_normalize},
                        {"find", l_find},
                        {"root", l_root},
                        {nullptr, nullptr}};

}  // namespace

void script_open_fs() { script_register("fs", kFs); }
