#include "parser.hpp"
#include <dlfcn.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>
#include "job.hpp"
#include "json.hpp"
#include "script.hpp"

namespace {
struct ParserInfo {
    std::string_view name, url, subdir, exts;
};
constexpr ParserInfo kParsers[] = {
#include "parsers.inc"
};

std::map<std::string, const TSLanguage*, std::less<>> g_loaded;
std::set<std::string, std::less<>> g_busy;
std::string g_err;

const ParserInfo* find(std::string_view n) {
    for(const auto& p : kParsers)
        if(p.name == n) return &p;
    return nullptr;
}

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

struct Meta {
    const ParserInfo* info;
    std::vector<std::string> types;
};
std::vector<Meta> g_meta;
bool g_meta_ok = false;

std::string lower(std::string s) {
    for(char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
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

echo "cloning $NAME"
git clone --depth 1 -q "$URL" "$tmp/repo"
root="$tmp/repo"
cd "$root/$SUB"
if [ ! -f src/parser.c ]; then
  echo "$NAME: repo has no src/parser.c (needs tree-sitter generate)"
  exit 1
fi
for d in . "$root"; do
    for m in tree-sitter.json package.json; do
        if [ -f "$d/$m" ]; then cp -f "$d/$m" "$DATA/parsers/$NAME.meta.json";
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
q="$root/$SUB/queries"; [ -d "$q" ] || q="$root/queries"
if [ -f "$q/highlights.scm" ]; then cp -f "$q/highlights.scm" "$DATA/queries/$NAME/"; fi
echo "done $NAME"

NVTS=https://raw.githubusercontent.com/nvim-treesitter/nvim-treesitter/main/runtime/queries
fetch_indents() {
  qd="$DATA/queries/$1"; mkdir -p "$qd"
  curl -fsSL --max-time 20 "$NVTS/$1/indents.scm" -o "$qd/indents.scm.new" 2>/dev/null \
    || { rm -f "$qd/indents.scm.new"; echo "no indents query for $1"; return 0; }
  mv -f "$qd/indents.scm.new" "$qd/indents.scm"
  for b in $(sed -n 's/^; inherits: *//p' "$qd/indents.scm" | head -1 | tr ',?' '  '); do
    [ -f "$DATA/queries/$b/indents.scm" ] || fetch_indents "$b"
  done
}
fetch_indents "$NAME"
)SH";

std::string script(const ParserInfo& p) {
    std::string s = "NAME=" + sh_quote(p.name) + "\nURL=" + sh_quote(p.url) +
                    "\nSUB=" + sh_quote(p.subdir) +
                    "\nDATA=" + sh_quote(data_dir()) + "\n";
    s += kBody;
    return s;
}

}  // namespace

bool parser_known(std::string_view lang) { return find(lang) != nullptr; }
bool parser_installed(std::string_view lang) {
    return access(so_path(lang).c_str(), R_OK) == 0;
}
std::string parser_error() { return g_err; }

std::string_view parser_lang_for_ext(std::string_view ext) {
    for(const auto& p : kParsers) {
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
    for(const auto& p : kParsers) {
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
