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

#include "filetype.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
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
    {".zlogout", "zsh"},
    {".zsh_aliases", "zsh"},
    {".zsh_functions", "zsh"},
    // no leading dot: stow/home-manager sources, /etc/zshrc, /etc/bashrc
    {"zshrc", "zsh"},
    {"zshenv", "zsh"},
    {"zprofile", "zsh"},
    {"zlogin", "zsh"},
    {"zlogout", "zsh"},
    {"bashrc", "bash"},
    {"bash.bashrc", "bash"},
    {"bash_profile", "bash"},
    {"bash_aliases", "bash"},
    {"profile", "bash"},
    {".xinitrc", "bash"},
    {".xprofile", "bash"},
    {".xsession", "bash"},
    {".envrc", "bash"},
    {"config.fish", "fish"},
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
    {"tmux.conf.", "tmux"},
    {".tmux.conf.", "tmux"},
    {".zshrc.", "zsh"},
    {".bashrc.", "bash"},
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

// ---- directory, wrapper and content rules -----------------------------------
// The nearest directory in this list decides the language of a file the
// extension can't place (~/.config/zsh/aliases, tmux/keys.conf, fish/functions/x).
struct DirRule {
    std::string_view dir, lang;
};
constexpr DirRule kDirs[] = {
    {"zsh", "zsh"},   {".zsh", "zsh"},   {"zshrc.d", "zsh"},
    {"bash", "bash"}, {".bash", "bash"}, {"bashrc.d", "bash"},
    {"bash-completion", "bash"},         {"profile.d", "bash"},
    {"fish", "fish"},
    {"tmux", "tmux"}, {".tmux", "tmux"},
};

// Extensions that say "a config of the directory's kind", not a language.
bool generic_ext(std::string_view e) {
    for(std::string_view g : {"", ".conf", ".local", ".inc", ".bak", ".orig",
                              ".old", ".dist", ".example", ".sample", ".tmpl"})
        if(e == g) return true;
    return false;
}

bool shellish(std::string_view l) {
    return l == "bash" || l == "zsh" || l == "fish";
}

std::string_view fallback_for(std::string_view l) {
    for(const auto& [a, b] : kFallback)
        if(l == a) return b;
    return {};
}

// Absolute so a bare "aliases" opened from ~/.config/zsh still sees the
// directory; nothing here touches the disk except the cwd lookup.
std::string_view by_dir(std::string_view path, std::string* hit = nullptr) {
    std::error_code ec;
    const std::filesystem::path p =
        std::filesystem::absolute(std::filesystem::path(path), ec);
    if(ec) return {};
    const std::string name = p.filename().string();
    // README, LICENSE, COPYING: all-caps names are prose, not scripts
    if(name.empty() || std::ranges::none_of(name, [](unsigned char c) {
           return std::islower(c);
       }))
        return {};
    if(!generic_ext(lower(p.extension().string()))) return {};
    for(auto d = p.parent_path(); d.has_relative_path(); d = d.parent_path()) {
        const std::string c = lower(d.filename().string());
        for(const DirRule& r : kDirs)
            if(c == r.dir) {
                if(hit) *hit = c;
                return r.lang;
            }
    }
    return {};
}

// ".conf" of "statusbar.conf"; empty for "zshrc" and for dotfiles (".zshrc").
std::string ext_of(const std::string& lb) {
    const size_t d = lb.rfind('.');
    return d == std::string::npos || d == 0 ? std::string() : lb.substr(d);
}

// /nix/store/<32 char hash>-tmux.conf -> tmux.conf
std::string_view unstore(std::string_view b) {
    if(b.size() > 33 && b[32] == '-' &&
       std::ranges::all_of(b.substr(0, 32), [](unsigned char c) {
           return std::isalnum(c);
       }))
        return b.substr(33);
    return b;
}

// foo.zsh.bak -> foo.zsh, tmux.conf.local -> tmux.conf, .zshrc~ -> .zshrc
std::string_view unwrap(std::string_view b) {
    for(std::string_view s : {".local", ".bak", ".orig", ".old", ".dist",
                              ".example", ".sample", ".tmpl", "~"})
        if(b.size() > s.size() && b.ends_with(s))
            return b.substr(0, b.size() - s.size());
    return b;
}

// The file name as written, without a store hash, without a backup/override
// suffix; lower-cased and de-duplicated.
std::vector<std::string> variants(std::string_view base) {
    std::vector<std::string> v;
    const auto add = [&](std::string_view s) {
        std::string l = lower(s);
        if(!l.empty() && std::ranges::find(v, l) == v.end())
            v.push_back(std::move(l));
    };
    const std::string_view u = unstore(base);
    add(base);
    add(u);
    add(unwrap(u));
    return v;
}

// The name rules for one spelling of the file name, in two passes around the
// directory rules.
//   strict: exact names, prefixes, extensions we ship, and (for a specific
//           extension) the file-types installed grammars declare
//   loose:  what is left: for a generic or missing extension the grammars'
//           own file-types ("conf", "config", "rc" are claimed by half the
//           grammars out there), then the synced linguist extensions
// A generic extension (.conf, .local, ...) says "a config file", not which
// one, so it has to meet the directory and the content before the guesses.
std::string by_base(const std::string& lb, bool loose, std::string* why) {
    const bool generic = generic_ext(ext_of(lb));
    const auto got = [&](std::string l, const char* w) {
        if(why) *why = w;
        return l;
    };
    if(loose) {
        if(generic)
            if(const auto l = parser_lang_for_file(lb); !l.empty())
                return got(std::string(l), "grammar file-types");
        for(size_t d = lb.find('.', 1); d != std::string::npos;
            d = lb.find('.', d + 1))
            if(const auto* l = find(g_t.gexts, lb.substr(d)))
                return got(*l, "linguist extension");
        return {};
    }
    if(const auto* l = find(g_t.names, lb)) return got(*l, "file name");
    if(const auto* l = find(g_t.gnames, lb)) return got(*l, "linguist file name");
    for(const auto& [pre, lang] : kPrefixes)
        if(lb.starts_with(pre)) return got(std::string(lang), "file name prefix");
    // longest extension first: foo.d.ts tries ".d.ts" then ".ts"
    for(size_t d = lb.find('.', 1); d != std::string::npos;
        d = lb.find('.', d + 1))
        if(const auto* l = find(g_t.exts, lb.substr(d)))
            return got(*l, "extension");
    if(!generic)
        if(const auto l = parser_lang_for_file(lb); !l.empty())
            return got(std::string(l), "grammar file-types");
    return {};
}

struct PathHit {
    std::string lang;
    std::string why;
    bool dir = false;    // decided by the directory only
    bool loose = false;  // a guess for a generic extension
    // weak hits can be overruled by what the file says about itself
    bool weak() const { return dir || loose; }
};

PathHit by_path(std::string_view path) {
    const std::string_view base = path.substr(
        path.rfind('/') == std::string_view::npos ? 0 : path.rfind('/') + 1);
    if(base.empty()) return {};
    const std::vector<std::string> vs = variants(base);
    std::string why;
    for(const std::string& v : vs)
        if(std::string l = by_base(v, false, &why); !l.empty())
            return {std::move(l), why, false, false};
    std::string dn;
    if(const auto l = by_dir(path, &dn); !l.empty())
        return {std::string(l), "directory '" + dn + "'", true, false};
    for(const std::string& v : vs)
        if(std::string l = by_base(v, true, &why); !l.empty())
            return {std::move(l), why, false, generic_ext(ext_of(v))};
    return {};
}

// Dotfile managers link into the store (or a dotfiles repo): when the link's
// own spelling says nothing, the target's name and directory may.
PathHit by_path_follow(std::string_view path) {
    std::string cur(path);
    for(int hop = 0;; ++hop) {
        PathHit h = by_path(cur);
        if(!h.lang.empty() && hop > 0) h.why += " of the symlink target";
        if(!h.lang.empty() || hop >= 4) return h;
        std::error_code ec;
        const std::filesystem::path p(cur);
        if(!std::filesystem::is_symlink(p, ec)) return {};
        std::filesystem::path t = std::filesystem::read_symlink(p, ec);
        if(ec) return {};
        if(t.is_relative()) t = p.parent_path() / t;
        cur = t.lexically_normal().string();
    }
}

// "#!/usr/bin/env zsh", "#!/nix/store/<hash>-bash-5.2/bin/bash", and
// "#! nix-shell -i python3 -p ..." (the -i interpreter is the language).
std::string by_shebang(std::string_view head) {
    const auto lookup = [](const std::string& in) -> std::string {
        if(const auto* l = find(g_t.interps, in)) return *l;
        if(const auto* l = find(g_t.ginterps, in)) return *l;
        return {};
    };
    const std::string in = shebang_interp(head.substr(0, head.find('\n')));
    if(in.empty()) return {};
    if(in != "nix-shell") return lookup(in);
    for(size_t pos = 0; pos < head.size();) {
        size_t e = head.find('\n', pos);
        if(e == std::string_view::npos) e = head.size();
        const std::string_view line = head.substr(pos, e - pos);
        pos = e + 1;
        const size_t i = line.find(" -i ");
        if(!line.starts_with("#!") || i == std::string_view::npos) continue;
        std::string_view rest = line.substr(i + 4);
        while(rest.starts_with(' ')) rest.remove_prefix(1);
        rest = rest.substr(0, rest.find(' '));
        return lookup(shebang_interp("#!/usr/bin/env " + std::string(rest)));
    }
    return {};
}

// "# vim: set ft=zsh :" in the first lines. Only languages we know, so a
// stray "ft=conf" can't switch highlighting off.
std::string by_modeline(std::string_view head) {
    int seen = 0;
    for(size_t pos = 0; pos < head.size() && seen < 5; ++seen) {
        size_t e = head.find('\n', pos);
        if(e == std::string_view::npos) e = head.size();
        const std::string_view line = head.substr(pos, e - pos);
        pos = e + 1;
        if(line.find("vim:") == std::string_view::npos &&
           line.find("vi:") == std::string_view::npos &&
           line.find("ex:") == std::string_view::npos)
            continue;
        for(std::string_view key : {"filetype=", "ft="}) {
            const size_t k = line.find(key);
            if(k == std::string_view::npos ||
               (k > 0 && std::isalnum(static_cast<unsigned char>(line[k - 1]))))
                continue;
            const size_t s = k + key.size();
            size_t t = s;
            while(t < line.size() &&
                  (std::isalnum(static_cast<unsigned char>(line[t])) ||
                   line[t] == '_' || line[t] == '-' || line[t] == '+'))
                ++t;
            if(t == s) continue;
            std::string l = alias_of(std::string(line.substr(s, t - s)));
            if(parser_known(l) || parser_builtin(l)) return l;
        }
    }
    return {};
}

// Constructs only one language has; weakest signal, used when nothing better
// placed the file.
std::string by_sniff(std::string_view head, std::string_view ext) {
    const std::string h = "\n" + std::string(head);
    const auto any = [&](std::initializer_list<std::string_view> ks) {
        for(std::string_view k : ks)
            if(h.find(k) != std::string::npos) return true;
        return false;
    };
    if(any({"\n#compdef", "\n#autoload", "\nsetopt ", "\nunsetopt ",
            "\nzstyle ", "\nbindkey ", "\nzmodload ", "\nemulate "}))
        return "zsh";
    if(ext == ".conf" &&
       any({"\nset -g ", "\nset -ga ", "\nset-option ", "\nbind-key ",
            "\nsource-file ", "\nset-window-option ", "\nsetw "}))
        return "tmux";
    if(any({"\nshopt "})) return "bash";
    return {};
}

// The language the file is, alias-resolved but not fallback-resolved. `why`
// names the rule that decided it (for :ft).
std::string wanted(std::string_view path, std::string_view head,
                   std::string* why = nullptr) {
    if(!g_t_ok) load();
    const auto done = [&](std::string l, std::string w) {
        if(why) *why = std::move(w);
        return alias_of(std::move(l));
    };
    if(std::string m = by_modeline(head); !m.empty())
        return done(std::move(m), "modeline");
    PathHit r = by_path_follow(path);
    if(const std::string s = by_shebang(head); !s.empty()) {
        // a shebang is stronger than a directory, a guess for a generic
        // extension, or a sibling shell's extension
        if(r.lang.empty() || r.weak() || (shellish(r.lang) && shellish(s)))
            return done(s, "shebang");
    } else if(r.lang.empty() || r.weak()) {
        const std::string ext =
            lower(std::filesystem::path(path).extension().string());
        const std::string n = by_sniff(head, ext);
        // the directory only gives way to a language of the same family
        if(!n.empty() &&
           (r.lang.empty() || r.loose || (shellish(r.lang) && shellish(n))))
            return done(n, "file content");
    }
    if(r.lang.empty()) return {};
    return done(std::move(r.lang), std::move(r.why));
}

}  // namespace

namespace ft {

std::string normalize(std::string s) {
    s = alias_of(std::move(s));
    if(parser_usable(s)) return s;
    std::string_view alt = fallback_for(s);
    for(int i = 0; i < 4 && !alt.empty(); ++i, alt = fallback_for(alt))
        if(parser_usable(alt)) return std::string(alt);
    return s;
}

std::string_view detect(std::string_view path, std::string_view head) {
    const std::string w = wanted(path, head);
    return w.empty() ? std::string_view{} : intern(normalize(w));
}

std::string explain(std::string_view path, std::string_view head) {
    std::string why;
    const std::string w = wanted(path, head, &why);
    if(w.empty()) return "none (no rule matched)";
    const Resolved r = resolve(path, head);
    std::string o = w + " via " + why + "; parser: ";
    if(r.use.empty()) o += parser_known(w) ? "not installed" : "none exists";
    else if(r.use == r.want) o += std::string(r.use);
    else o += std::string(r.use) + " (fallback, " + w + " not installed)";
    return o;
}

Resolved resolve(std::string_view path, std::string_view head) {
    const std::string w = wanted(path, head);
    if(w.empty()) return {};
    const std::string_view want = intern(w);
    if(parser_usable(want)) return {want, want};
    std::string_view alt = fallback_for(want);
    for(int i = 0; i < 4 && !alt.empty(); ++i, alt = fallback_for(alt))
        if(parser_usable(alt)) return {want, alt};
    return {want, {}};
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
