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

#include "parser.hpp"
#include <dlfcn.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <lua.hpp>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#include <tree_sitter/api.h>
#include "filetype.hpp"
#include "job.hpp"
#include "json.hpp"
#include "script.hpp"

namespace {
constexpr ParserInfo kParsers[] = {
#include "parsers.inc"
};

std::map<std::string, const TSLanguage*, std::less<>> g_loaded;
std::set<std::string, std::less<>> g_busy;
std::string g_err;

std::string data_dir() {
    if(const char* x = std::getenv("XDG_DATA_HOME"); x && *x)
        return std::string(x) + "/winter";
    const char* h = std::getenv("HOME");
    return std::string(h ? h : ".") + "/.local/share/winter";
}
std::string so_path(std::string_view n) {
    return data_dir() + "/parsers/" + std::string(n) + ".so";
}

std::string sh_quote(std::string_view s) {
    std::string o = "'";
    for(const char c : s) {
        if(c == '\'') o += "'\\''";
        else o += c;
    }
    return o + "'";
}

std::string read_file(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

std::string last_line(std::string_view s) {
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.remove_suffix(1);
    const auto nl = s.rfind('\n');
    return std::string(nl == std::string_view::npos ? s : s.substr(nl + 1));
}

std::vector<Meta> g_meta;
bool g_meta_ok = false;

std::string lower(std::string s) {
    for(char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---- grammar table ----------------------------------------------------------
// kParsers is the compiled-in seed. grammars.v2.tsv
// ("name<TAB>url<TAB>subdir<TAB>rev" per line, written by parser_refresh)
// replaces url/subdir/rev; exts always come from the seed, and seed rows the
// registry lacks (tmux) are kept.
constexpr std::string_view kLinguistUrl =
    "https://raw.githubusercontent.com/github-linguist/linguist/main/lib/"
    "linguist/languages.yml";
constexpr std::string_view kRegistryUrl =
    "https://raw.githubusercontent.com/nvim-treesitter/nvim-treesitter/main/"
    "lua/nvim-treesitter/parsers.lua";
constexpr std::string_view kBuiltin[] = {"c", "cpp", "java", "lua", "rust"};

// Old blobs are never freed: parser_lang_for_* hand out views into them.
std::deque<std::string> g_blobs;
std::vector<ParserInfo> g_table;
bool g_table_ok = false;
bool g_refreshing = false;

std::string grammars_path() { return data_dir() + "/grammars.v2.tsv"; }

void load_table() {
    g_table_ok = true;
    g_meta_ok = false;  // Meta points into the old table
    g_table.assign(std::begin(kParsers), std::end(kParsers));
    std::string txt = read_file(grammars_path());
    if(txt.empty()) return;
    const std::string_view blob = g_blobs.emplace_back(std::move(txt));
    std::vector<ParserInfo> reg;
    for(size_t pos = 0; pos < blob.size();) {
        size_t end = blob.find('\n', pos);
        if(end == std::string_view::npos) end = blob.size();
        const std::string_view line = blob.substr(pos, end - pos);
        pos = end + 1;
        std::string_view f[4];
        size_t n = 0, s = 0;
        while(n < 4) {
            const size_t t = line.find('\t', s);
            f[n++] = line.substr(s, t == std::string_view::npos ? t : t - s);
            if(t == std::string_view::npos) break;
            s = t + 1;
        }
        if(n == 4 && !f[0].empty() && !f[1].empty())
            reg.push_back({f[0], f[1], f[2], {}, f[3]});
    }
    std::vector<ParserInfo> out;
    for(ParserInfo r : reg) {
        if(std::ranges::find(kBuiltin, r.name) != std::end(kBuiltin)) continue;
        for(const ParserInfo& s : kParsers)
            if(s.name == r.name) r.exts = s.exts;
        out.push_back(r);
    }
    if(out.empty()) return;  // unreadable cache: keep the seed
    for(const ParserInfo& s : kParsers)
        if(std::ranges::none_of(
               out, [&](const ParserInfo& o) { return o.name == s.name; }))
            out.push_back(s);
    std::ranges::sort(out, {}, &ParserInfo::name);
    g_table = std::move(out);
}

const std::vector<ParserInfo>& table() {
    if(!g_table_ok) load_table();
    return g_table;
}

const ParserInfo* find(std::string_view n) {
    for(const ParserInfo& p : table())
        if(p.name == n) return &p;
    return nullptr;
}

void load_meta() {
    g_meta_ok = true;
    g_meta.clear();
    constexpr std::string_view suf = ".meta.json";
    std::error_code ec;
    for(const auto& de :
        std::filesystem::directory_iterator(data_dir() + "/parsers", ec)) {
        const std::string fn = de.path().filename().string();
        if(!fn.ends_with(suf)) continue;
        const ParserInfo* info =
            find(std::string_view(fn).substr(0, fn.size() - suf.size()));
        const auto j = Json::parse(read_file(de.path().string()));
        if(!info || !j) continue;
        const Json& list = (*j)["grammars"].is_array() ? (*j)["grammars"]
                                                       : (*j)["tree-sitter"];
        if(!list.is_array()) continue;
        for(const Json& g : list.items()) {
            if(g.has("name") && g["name"].as_str() != info->name) continue;
            if(!g["file-types"].is_array()) continue;
            Meta m{info, {}};
            for(const Json& t : g["file-types"].items()) {
                std::string s = lower(std::string(t.as_str()));
                if(s.starts_with('.')) s.erase(0, 1);
                if(!s.empty()) m.types.push_back(std::move(s));
            }
            if(!m.types.empty()) g_meta.push_back(std::move(m));
        }
    }
    std::ranges::sort(g_meta, {}, [](const Meta& m) { return m.info->name; });
}

std::string query_for(std::string_view lang, std::string_view kind, int depth) {
    const std::string rel =
        "/queries/" + std::string(lang) + "/" + std::string(kind) + ".scm";
    std::string txt = read_file(script_config_dir() + rel);
    if(txt.empty()) txt = read_file(data_dir() + rel);
    std::string pre;
    std::istringstream in(txt);
    for(std::string line;
        depth < 4 && std::getline(in, line) && line.starts_with(';');) {
        constexpr std::string_view key = "; inherits:";
        if(!line.starts_with(key)) continue;
        std::istringstream list(line.substr(key.size()));
        for(std::string b; std::getline(list, b, ',');) {
            b.erase(std::remove_if(b.begin(),
                                   b.end(),
                                   [](unsigned char c) {
                                       return std::isspace(c) || c == '?';
                                   }),
                    b.end());
            if(!b.empty()) pre += query_for(b, kind, depth + 1) + "\n";
        }
    }
    return pre + txt;
}

constexpr std::string_view kBody = R"SH(
set -eu
export GIT_TERMINAL_PROMPT=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Fail with the tool's name up front: on NixOS none of these is guaranteed to
# be in PATH, and "cc: not found" halfway through a build is easy to miss.
need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "$NAME: '$1' not found in PATH (NixOS: nix-shell -p $2; Arch: pacman -S $3)"
        exit 1; }
}
need git git git
need curl curl curl
need "${CC:-cc}" gcc gcc

D="${XDG_DATA_HOME:-$HOME/.local/share}/winter/queries"
N=https://raw.githubusercontent.com/nvim-treesitter/nvim-treesitter/main/runtime/queries
for l in c cpp java lua rust ecma jsx html_tags; do
    mkdir -p "$D/$l"
    for k in highlights injections indents; do
        curl -fsSL "$N/$l/$k.scm" -o "$D/$l/$k.scm" || rm -f "$D/$l/$k.scm"
    done
done

echo "cloning $NAME"
if [ -n "$REV" ] \
   && git init -q "$tmp/repo" \
   && git -C "$tmp/repo" fetch --depth 1 -q "$URL" "$REV" \
   && git -C "$tmp/repo" checkout -q FETCH_HEAD; then
    :
else
    rm -rf "$tmp/repo"
    git clone --depth 1 -q "$URL" "$tmp/repo"
    REV=
fi
root="$tmp/repo"
cd "$root/$SUB"
if [ ! -f src/parser.c ]; then
    # Many grammars (tmux, latex, swift, ...) only commit grammar.js.
    if [ -f src/grammar.json ]; then gen=src/grammar.json
    elif [ -f grammar.js ]; then gen=
    else echo "$NAME: no src/parser.c and no grammar to generate it from"; exit 1; fi
    command -v tree-sitter >/dev/null 2>&1 || {
        echo "$NAME: repo has no src/parser.c; install the tree-sitter CLI to generate it (NixOS: nix-shell -p tree-sitter nodejs; Arch: pacman -S tree-sitter-cli nodejs)"
        exit 1; }
    if [ -z "$gen" ]; then command -v node >/dev/null 2>&1 || {
        echo "$NAME: generating from grammar.js needs node (NixOS: nix-shell -p nodejs; Arch: pacman -S nodejs)"; exit 1; }; fi
    echo "generating $NAME"
    tree-sitter generate --abi "$ABI" $gen >"$tmp/gen.log" 2>&1 || {
        echo "$NAME: tree-sitter generate failed: $(tail -n 1 "$tmp/gen.log")"; exit 1; }
fi

mkdir -p "$DATA/parsers" "$DATA/queries/$NAME"
for d in . "$root"; do
    for m in tree-sitter.json package.json; do
        if [ -f "$d/$m" ]; then
            cp -f "$d/$m" "$DATA/parsers/$NAME.meta.json"
            break 2
        fi
    done
done

echo "compiling $NAME"
cc=${CC:-cc}; cxx=${CXX:-c++}; link=$cc
"$cc" -fPIC -O2 -Isrc -c src/parser.c -o parser.o
objs=parser.o
if [ -f src/scanner.c ]; then
  "$cc" -fPIC -O2 -Isrc -c src/scanner.c -o scanner_c.o
  objs="$objs scanner_c.o"
fi
for s in src/scanner.cc src/scanner.cpp; do
  if [ -f "$s" ]; then
    "$cxx" -fPIC -O2 -Isrc -c "$s" -o scanner_cc.o
    objs="$objs scanner_cc.o"; link=$cxx
  fi
done
mkdir -p "$DATA/parsers" "$DATA/queries/$NAME"
"$link" -shared -o "$DATA/parsers/$NAME.so.new" $objs
echo "installing $NAME"
mv -f "$DATA/parsers/$NAME.so.new" "$DATA/parsers/$NAME.so"
printf '%s\n' "$REV" > "$DATA/parsers/$NAME.rev"
q="$root/$SUB/queries"; [ -d "$q" ] || q="$root/queries"
if [ -f "$q/highlights.scm" ]; then cp -f "$q/highlights.scm" "$DATA/queries/$NAME/"; fi
echo "done $NAME"

NVTS=https://raw.githubusercontent.com/nvim-treesitter/nvim-treesitter/main/runtime/queries
fetch_q() {
    qd="$DATA/queries/$1"; mkdir -p "$qd"
    curl -fsSL --max-time 20 "$NVTS/$1/$2.scm" -o "$qd/$2.scm.new" 2>/dev/null \
        || { rm -f "$qd/$2.scm.new"; echo "no $2 query for $1"; return 0; }
    mv -f "$qd/$2.scm.new" "$qd/$2.scm"
    for b in $(sed -n 's/^; inherits: *//p' "$qd/$2.scm" | head -1 | tr ',?' '  '); do
        [ -f "$DATA/queries/$b/$2.scm" ] || fetch_q "$b" "$2"
    done
}
for k in indents highlights injections; do fetch_q "$NAME" "$k"; done
)SH";

std::string script(const ParserInfo& p) {
    std::string s = "NAME=" + sh_quote(p.name) + "\nURL=" + sh_quote(p.url) +
                    "\nSUB=" + sh_quote(p.subdir) + "\nREV=" + sh_quote(p.rev) +
                    "\nABI=" + std::to_string(TREE_SITTER_LANGUAGE_VERSION) +
                    "\nDATA=" + sh_quote(data_dir()) + "\n";
    s += kBody;
    return s;
}

std::string lua_str(lua_State* L, const char* key) {
    lua_getfield(L, -1, key);
    std::string s = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return s;
}

bool registry_to_tsv(const std::string& src, std::string& out,
                     std::string& err) {
    lua_State* L = luaL_newstate();
    if(!L) {
        err = "out of memory";
        return false;
    }
    lua_sethook(
        L,
        [](lua_State* l, lua_Debug*) {
            luaL_error(l, "parsers.lua ran too long");
        },
        LUA_MASKCOUNT,
        10'000'000);
    if(luaL_loadbuffer(L, src.data(), src.size(), "=parsers.lua") != LUA_OK ||
       lua_pcall(L, 0, 1, 0) != LUA_OK || !lua_istable(L, -1)) {
        const char* m =
            lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
        err = m ? m : "parsers.lua did not return a table";
        lua_close(L);
        return false;
    }
    size_t rows = 0;
    for(lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
        if(lua_type(L, -2) != LUA_TSTRING || !lua_istable(L, -1)) continue;
        const std::string name = lua_tostring(L, -2);
        lua_getfield(L, -1, "tier");
        const bool unsupported =
            lua_isnumber(L, -1) && lua_tointeger(L, -1) == 4;
        lua_pop(L, 1);
        lua_getfield(L, -1, "install_info");
        // generate / generate_from_json rows stay: the installer builds
        // src/parser.c itself when the repo doesn't ship one.
        if(!unsupported && lua_istable(L, -1)) {
            const std::string url = lua_str(L, "url");
            if(!url.empty()) {
                out += name + '\t' + url + '\t' + lua_str(L, "location") +
                       '\t' + lua_str(L, "revision") + '\n';
                ++rows;
            }
        }
        lua_pop(L, 1);
    }
    lua_close(L);
    if(rows < 50) {
        err = "parsers.lua gave only " + std::to_string(rows) + " grammars";
        return false;
    }
    return true;
}

bool build_grammars(std::string& err) {
    std::string tsv;
    if(!registry_to_tsv(read_file(data_dir() + "/parsers.lua"), tsv, err))
        return false;
    const std::string path = grammars_path();
    {
        std::ofstream o(path + ".new", std::ios::binary | std::ios::trunc);
        o << tsv;
        o.close();
        if(o.fail()) {
            err = "can't write " + path;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(path + ".new", path, ec);
    if(ec) {
        err = ec.message();
        return false;
    }
    return true;
}

bool outdated(const ParserInfo& p) {
    if(p.rev.empty() || !parser_installed(p.name)) return false;
    std::string have =
        read_file(data_dir() + "/parsers/" + std::string(p.name) + ".rev");
    while(!have.empty() && (have.back() == '\n' || have.back() == '\r'))
        have.pop_back();
    return have != p.rev;
}

struct UpdateRun {
    std::deque<std::string> todo;
    int ok = 0, bad = 0;
    UpdateEvents ev;
};
std::optional<UpdateRun> g_upd;

void update_next() {
    if(!g_upd) return;
    if(g_upd->todo.empty()) {
        const UpdateEvents ev = std::move(g_upd->ev);
        const int ok = g_upd->ok, bad = g_upd->bad;
        g_upd.reset();
        if(ev.done) ev.done(ok, bad);
        return;
    }
    const std::string lang = std::move(g_upd->todo.front());
    g_upd->todo.pop_front();
    const JobId id = parser_install(
        lang,
        [lang](std::string_view line) {
            if(g_upd && g_upd->ev.progress) g_upd->ev.progress(lang, line);
        },
        [lang](bool ok, std::string msg) {
            if(!g_upd) return;
            (ok ? g_upd->ok : g_upd->bad)++;
            if(g_upd->ev.each) g_upd->ev.each(lang, ok, msg);
            update_next();
        });
    if(!id) {
        ++g_upd->bad;
        if(g_upd->ev.each) g_upd->ev.each(lang, false, "already running");
        update_next();
    }
}

}  // namespace

bool parser_known(std::string_view lang) { return find(lang) != nullptr; }
bool parser_builtin(std::string_view lang) {
    return std::ranges::find(kBuiltin, lang) != std::end(kBuiltin);
}
bool parser_usable(std::string_view lang) {
    return parser_builtin(lang) || parser_installed(lang);
}
std::string parser_data_dir() { return data_dir(); }
bool parser_installed(std::string_view lang) {
    return access(so_path(lang).c_str(), R_OK) == 0;
}
std::string parser_error() { return g_err; }

std::string_view parser_lang_for_ext(std::string_view ext) {
    for(const auto& p : table()) {
        std::istringstream in{std::string(p.exts)};
        for(std::string e; in >> e;)
            if(e == ext) return p.name;
    }
    return {};
}

std::string_view parser_lang_for_file(std::string_view base) {
    if(!g_meta_ok) load_meta();
    const std::string b = lower(std::string(base));
    for(const Meta& m : g_meta)
        for(const std::string& t : m.types)
            if(b == t || (b.size() > t.size() && b.ends_with(t) &&
                          b[b.size() - t.size() - 1] == '.'))
                return m.info->name;
    return {};
}

const TSLanguage* parser_load(std::string_view lang) {
    if(const auto it = g_loaded.find(lang); it != g_loaded.end())
        return it->second;
    if(!find(lang) || !parser_installed(lang)) return nullptr;
    void* h = dlopen(so_path(lang).c_str(), RTLD_NOW | RTLD_LOCAL);
    if(!h) {
        g_err = dlerror();
        return nullptr;
    }
    using Fn = const TSLanguage* (*)();
    const auto fn = reinterpret_cast<Fn>(
        dlsym(h, ("tree_sitter_" + std::string(lang)).c_str()));
    if(!fn) {
        g_err = "no tree_sitter_" + std::string(lang) + " in " + so_path(lang);
        dlclose(h);
        return nullptr;
    }
    return g_loaded[std::string(lang)] = fn();
}

std::string parser_query(std::string_view lang, std::string_view kind) {
    return query_for(lang, kind, 0);
}

std::string parser_list() {
    std::string o;
    for(const auto& p : table()) {
        if(!o.empty()) o += ' ';
        o += p.name;
        if(parser_installed(p.name)) o += '*';
    }
    return o;
}

JobId parser_install(std::string_view lang,
                     std::function<void(std::string_view)> progress,
                     std::function<void(bool, std::string)> done) {
    const ParserInfo* p = find(lang);
    if(!p || g_busy.contains(lang)) return 0;
    const std::string name(p->name);
    const JobId id =
        job_start({.cmd = script(*p),
                   .on_data =
                       [progress](std::string_view chunk) {
                           if(progress) progress(last_line(chunk));
                       },
                   .on_exit =
                       [name, done](JobResult r) {
                           g_busy.erase(name);
                           const bool ok = r.code == 0;
                           if(ok) {
                               g_loaded.erase(name);
                               g_meta_ok = false;
                           }
                           if(done) done(ok, ok ? "" : last_line(r.out));
                       }});
    if(id) g_busy.insert(name);
    return id;
}

const std::vector<ParserInfo>& parser_table() { return table(); }

void parser_refresh(std::function<void(bool, std::string)> done) {
    const auto finish = [done](bool ok, std::string msg) {
        if(done) done(ok, std::move(msg));
    };
    if(g_refreshing) return finish(false, "already refreshing");
    const std::string d = sh_quote(data_dir());
    std::string cmd = "set -eu\nmkdir -p " + d + "\ncurl -fsSL --max-time 30 " +
                      sh_quote(kRegistryUrl) + " -o " + d +
                      "/parsers.lua.new\nmv -f " + d + "/parsers.lua.new " + d +
                      "/parsers.lua\n"
                      // filetype data is best effort: never fails the refresh
                      "curl -fsSL --max-time 30 " + sh_quote(kLinguistUrl) + " -o " + d +
                      "/languages.yml.new && mv -f " + d + "/languages.yml.new " + d +
                      "/languages.yml || rm -f " + d + "/languages.yml.new\n";
    g_refreshing = true;
    const JobId id = job_start(
        {.cmd = std::move(cmd), .on_exit = [finish](JobResult r) {
             g_refreshing = false;
             if(r.code != 0)
                 return finish(false, "fetch failed: " + last_line(r.out));
             std::string err;
             if(!build_grammars(err)) return finish(false, err);
             g_table_ok = false;  // reload on next use
             std::string msg = std::to_string(table().size()) + " grammars";
             std::string ferr;
             if(!ft::rebuild(ferr)) msg += "; filetypes: " + ferr;
             finish(true, std::move(msg));
         }});
    if(!id) {
        g_refreshing = false;
        finish(false, "can't start curl");
    }
}

bool parser_update(std::string_view lang, bool force, UpdateEvents ev) {
    if(g_upd || g_refreshing) return false;
    g_upd.emplace();
    g_upd->ev = std::move(ev);
    parser_refresh([only = std::string(lang), force](bool ok, std::string msg) {
        if(!g_upd) return;
        if(!ok && g_upd->ev.progress)
            g_upd->ev.progress(
                "", "grammar list: " + msg + " (using the cached one)");
        for(const ParserInfo& p : table()) {
            if((!only.empty() && p.name != only) || !parser_installed(p.name))
                continue;
            if(force || outdated(p)) g_upd->todo.emplace_back(p.name);
        }
        update_next();
    });
    return true;
}

// Silent and asynchronous; never blocks startup. Weekly unless the file is
// missing.
void parser_autorefresh() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto t = fs::last_write_time(grammars_path(), ec);
    if(!ec && fs::file_time_type::clock::now() - t < std::chrono::hours(24 * 7) &&
       fs::exists(data_dir() + "/filetypes.tsv", ec))
        return;
    parser_refresh({});
}
