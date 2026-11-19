#include "filetype.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>
#include "parser.hpp"

namespace {

using Pair = std::pair<std::string_view, std::string_view>;

std::string lower(std::string_view s) {
    std::string o(s);
    for(char& c : o)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

// Results are handed out as views; the pool never frees.
std::string_view intern(std::string s) {
    static std::set<std::string, std::less<>> pool;
    return *pool.insert(std::move(s)).first;
}

// ---- curated rules (always win over synced data) ---------------------------
constexpr Pair kNames[] = {
    {"makefile", "make"},
    {"gnumakefile", "make"},
    {"cmakelists.txt", "cmake"},
    {"meson.build", "meson"},
    {"meson.options", "meson"},
    {"pkgbuild", "bash"},
    {".bashrc", "bash"},
    {".bash_profile", "bash"},
    {".bash_login", "bash"},
    {".bash_logout", "bash"},
    {".bash_aliases", "bash"},
    {".profile", "bash"},
    {".zshrc", "zsh"},
    {".zshenv", "zsh"},
    {".zprofile", "zsh"},
    {".zlogin", "zsh"},
    {"tmux.conf", "tmux"},
    {".tmux.conf", "tmux"},
    {"dockerfile", "dockerfile"},
    {"containerfile", "dockerfile"},
    {"justfile", "just"},
    {".gitconfig", "git_config"},
    {".gitignore", "gitignore"},
    {".gitattributes", "gitattributes"},
    {".editorconfig", "editorconfig"},
};

constexpr Pair kPrefixes[] = {
    {"dockerfile.", "dockerfile"},
    {"containerfile.", "dockerfile"},
    {"makefile.", "make"},
};

constexpr Pair kExts[] = {
    {".lua", "lua"},
    {".c", "c"},
    {".h", "c"},
    {".cpp", "cpp"},
    {".cc", "cpp"},
    {".cxx", "cpp"},
    {".hpp", "cpp"},
    {".hh", "cpp"},
    {".java", "java"},
    {".rs", "rust"},
    {".json", "json"},
    {".jsonc", "json"},
    {".sh", "bash"},
    {".bash", "bash"},
    {".zsh", "zsh"},
    {".fish", "fish"},
    {".tmux", "tmux"},
    {".d.ts", "typescript"},
};

// Interpreter basename (version suffix stripped) -> language.
constexpr Pair kInterps[] = {
    {"sh", "bash"},   {"bash", "bash"},  {"dash", "bash"},  {"ash", "bash"},
    {"ksh", "bash"},  {"mksh", "bash"},  {"zsh", "zsh"},    {"fish", "fish"},
    {"python", "python"}, {"node", "javascript"}, {"nodejs", "javascript"},
    {"bun", "javascript"}, {"deno", "typescript"}, {"ruby", "ruby"},
    {"perl", "perl"}, {"lua", "lua"}, {"luajit", "lua"}, {"awk", "awk"},
    {"gawk", "awk"},  {"mawk", "awk"},   {"nawk", "awk"},   {"php", "php"},
    {"rscript", "r"}, {"tclsh", "tcl"},  {"nu", "nu"},
};

// Always rewritten (names people type, linguist aliases).
constexpr Pair kAlias[] = {
    {"sh", "bash"},         {"shell", "bash"},      {"shellscript", "bash"},
    {"shell-script", "bash"}, {"jsonc", "json"},    {"js", "javascript"},
    {"javascriptreact", "javascript"}, {"ts", "typescript"},
    {"typescriptreact", "tsx"}, {"py", "python"},   {"c++", "cpp"},
    {"cs", "c_sharp"},      {"c#", "c_sharp"},      {"yml", "yaml"},
    {"rs", "rust"},         {"md", "markdown"},     {"rb", "ruby"},
    {"golang", "go"},       {"tex", "latex"},       {"vimscript", "vim"},
    {"viml", "vim"},        {"htm", "html"},        {"patch", "diff"},
};

// Used only when the first language has no usable parser.
constexpr Pair kFallback[] = {
    {"zsh", "bash"},
    {"ksh", "bash"},
    {"json5", "json"},
};

std::string alias_of(std::string s) {
    s = lower(s);
    for(const auto& [a, b] : kAlias)
        if(s == a) return std::string(b);
    return s;
}

// ---- tables -----------------------------------------------------------------
using Map = std::unordered_map<std::string, std::string>;
struct Tables {
    Map names, exts, interps;          // curated + grammar table
    Map gnames, gexts, ginterps;       // synced from linguist
};
Tables g_t;
bool g_t_ok = false;

std::string tsv_path() { return parser_data_dir() + "/filetypes.tsv"; }

void put(Map& m, std::string_view k, std::string_view v) {
    if(!k.empty() && !v.empty()) m.emplace(lower(k), std::string(v));
}

void load() {
    g_t = {};
    g_t_ok = true;
    for(const auto& [k, v] : kNames) put(g_t.names, k, v);
    for(const auto& [k, v] : kExts) put(g_t.exts, k, v);
    for(const auto& [k, v] : kInterps) put(g_t.interps, k, v);
    for(const ParserInfo& p : parser_table()) {
        std::istringstream in{std::string(p.exts)};
        for(std::string e; in >> e;) put(g_t.exts, e, p.name);
    }
    std::ifstream in(tsv_path());
    for(std::string line; std::getline(in, line);) {
        const size_t a = line.find('\t');
        const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if(b == std::string::npos || a != 1) continue;
        const std::string_view key = std::string_view(line).substr(a + 1, b - a - 1);
        const std::string_view lang = std::string_view(line).substr(b + 1);
        switch(line[0]) {
        case 'e': put(g_t.gexts, key, lang); break;
        case 'n': put(g_t.gnames, key, lang); break;
        case 'i': put(g_t.ginterps, key, lang); break;
        default: break;
        }
    }
}

const std::string* find(const Map& m, const std::string& k) {
    const auto it = m.find(k);
    return it == m.end() ? nullptr : &it->second;
}

// First line of the form "#!/path/to/prog" or "#!/usr/bin/env [-S] [VAR=x] prog".
std::string shebang_interp(std::string_view l) {
    if(!l.starts_with("#!")) return {};
    l.remove_prefix(2);
    std::vector<std::string_view> tok;
    for(size_t i = 0; i < l.size();) {
        while(i < l.size() && (l[i] == ' ' || l[i] == '\t' || l[i] == '\r'))
            ++i;
        const size_t s = i;
        while(i < l.size() && l[i] != ' ' && l[i] != '\t' && l[i] != '\r') ++i;
        if(i > s) tok.push_back(l.substr(s, i - s));
    }
    const auto base = [](std::string_view t) {
        return t.substr(t.rfind('/') == std::string_view::npos
                            ? 0
                            : t.rfind('/') + 1);
    };
    size_t i = 0;
    if(tok.empty()) return {};
    std::string_view prog = base(tok[i++]);
    if(prog == "env") {
        while(i < tok.size() &&
              (tok[i].starts_with('-') ||
               tok[i].find('=') != std::string_view::npos))
            ++i;
        if(i >= tok.size()) return {};
        prog = base(tok[i]);
    }
    std::string p = lower(prog);
    size_t n = p.size();  // python3.11 -> python, lua5.4 -> lua, ksh93 -> ksh
    while(n > 1 && (std::isdigit(static_cast<unsigned char>(p[n - 1])) ||
                    p[n - 1] == '.'))
        --n;
    p.resize(n);
    return p;
}

}  // namespace

namespace ft {

std::string normalize(std::string s) {
    s = alias_of(std::move(s));
    if(parser_usable(s)) return s;
    for(const auto& [a, b] : kFallback)
        if(s == a && parser_usable(b)) return std::string(b);
    return s;
}

std::string_view detect(std::string_view path, std::string_view first_line) {
    if(!g_t_ok) load();
    const auto resolved = [](const std::string& lang) {
        return intern(normalize(lang));
    };
    const std::string_view base = path.substr(
        path.rfind('/') == std::string_view::npos ? 0 : path.rfind('/') + 1);
    if(!base.empty()) {
        const std::string lb = lower(base);
        if(const auto* l = find(g_t.names, lb)) return resolved(*l);
        if(const auto* l = find(g_t.gnames, lb)) return resolved(*l);
        for(const auto& [pre, lang] : kPrefixes)
            if(lb.starts_with(pre)) return resolved(std::string(lang));
        // longest extension first: foo.d.ts tries ".d.ts" then ".ts"
        for(size_t d = lb.find('.', 1); d != std::string::npos;
            d = lb.find('.', d + 1))
            if(const auto* l = find(g_t.exts, lb.substr(d))) return resolved(*l);
        if(const auto l = parser_lang_for_file(base); !l.empty())
            return resolved(std::string(l));
        for(size_t d = lb.find('.', 1); d != std::string::npos;
            d = lb.find('.', d + 1))
            if(const auto* l = find(g_t.gexts, lb.substr(d))) return resolved(*l);
    }
    if(const std::string in = shebang_interp(first_line); !in.empty()) {
        if(const auto* l = find(g_t.interps, in)) return resolved(*l);
        if(const auto* l = find(g_t.ginterps, in)) return resolved(*l);
    }
    return {};
}

void invalidate() { g_t_ok = false; }

// ---- linguist sync ----------------------------------------------------------
namespace {

struct Ling {
    std::string name, group;
    std::vector<std::string> aliases, exts, files, interps;
};

std::string trim(std::string_view s) {
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return std::string(s);
}

std::string unquote(std::string s) {
    s = trim(s);
    if(s.size() >= 2 && (s.front() == '"' || s.front() == '\'') &&
       s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

// Only the handful of keys we need; the file is regular enough for this.
std::vector<Ling> parse_linguist(const std::string& txt) {
    enum class K { none, aliases, exts, files, interps } k = K::none;
    std::vector<Ling> out;
    std::istringstream in(txt);
    for(std::string line; std::getline(in, line);) {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        if(line.empty() || line[0] == '#') continue;
        if(line[0] != ' ') {
            if(line.back() == ':' && line[0] != '-') {
                out.emplace_back();
                out.back().name = unquote(line.substr(0, line.size() - 1));
                k = K::none;
            }
            continue;
        }
        if(out.empty()) continue;
        if(line.starts_with("  - ")) {
            const std::string v = unquote(line.substr(4));
            Ling& L = out.back();
            switch(k) {
            case K::aliases: L.aliases.push_back(v); break;
            case K::exts: L.exts.push_back(v); break;
            case K::files: L.files.push_back(v); break;
            case K::interps: L.interps.push_back(v); break;
            case K::none: break;
            }
            continue;
        }
        const size_t colon = line.find(':');
        if(colon == std::string::npos || line.compare(0, 2, "  ") != 0) continue;
        const std::string key = trim(line.substr(0, colon));
        const std::string rest = trim(line.substr(colon + 1));
        k = K::none;
        if(rest.empty()) {
            if(key == "aliases") k = K::aliases;
            else if(key == "extensions") k = K::exts;
            else if(key == "filenames") k = K::files;
            else if(key == "interpreters") k = K::interps;
        } else if(key == "group") {
            out.back().group = unquote(rest);
        }
    }
    return out;
}

std::string slug(std::string s) {
    s = lower(s);
    if(s == "c++") return "cpp";
    if(s == "c#") return "c_sharp";
    for(char& c : s)
        if(c == ' ' || c == '-' || c == '.') c = '_';
    return s;
}

// Linguist language -> our grammar name ("" when we have none).
std::string map_lang(const Ling& L) {
    std::vector<std::string> cand{slug(L.name)};
    for(const std::string& a : L.aliases) cand.push_back(slug(a));
    if(!L.group.empty()) cand.push_back(slug(L.group));
    for(const std::string& c : cand) {
        const std::string a = alias_of(c);
        if(parser_known(a) || parser_builtin(a)) return a;
    }
    return {};
}

}  // namespace

bool rebuild(std::string& err) {
    const std::string dir = parser_data_dir();
    std::ifstream src(dir + "/languages.yml", std::ios::binary);
    const std::string txt{std::istreambuf_iterator<char>(src),
                          std::istreambuf_iterator<char>()};
    const std::vector<Ling> langs = parse_linguist(txt);
    if(langs.size() < 100) {
        err = "languages.yml missing or unreadable";
        return false;
    }
    // key -> set of languages; keys claimed by more than one are ambiguous
    // (.h, .m, .pl, ...) and dropped rather than guessed.
    std::map<std::string, std::set<std::string>> e, n, i;
    for(const Ling& L : langs) {
        const std::string lang = map_lang(L);
        if(lang.empty()) continue;
        for(const std::string& x : L.exts) e[lower(x)].insert(lang);
        for(const std::string& x : L.files) n[lower(x)].insert(lang);
        for(const std::string& x : L.interps) i[lower(x)].insert(lang);
    }
    std::string tsv;
    size_t rows = 0;
    const auto emit = [&](char kind, const auto& m) {
        for(const auto& [k, v] : m)
            if(v.size() == 1 && k.find_first_of("\t\n") == std::string::npos) {
                tsv += kind;
                tsv += '\t' + k + '\t' + *v.begin() + '\n';
                ++rows;
            }
    };
    emit('e', e);
    emit('n', n);
    emit('i', i);
    if(rows < 100) {
        err = "linguist gave only " + std::to_string(rows) + " rows";
        return false;
    }
    const std::string path = tsv_path();
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
    invalidate();
    return true;
}

}  // namespace ft
