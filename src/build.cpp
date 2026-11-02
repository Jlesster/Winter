#include "build.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::vector<BuildProvider>& table() {
    static std::vector<BuildProvider> t;
    return t;
}

bool path_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

std::string read_head(const fs::path& p, size_t n = 65536) {
    std::ifstream f(p, std::ios::binary);
    std::string s(n, '\0');
    f.read(s.data(), static_cast<std::streamsize>(n));
    s.resize(static_cast<size_t>(f.gcount()));
    return s;
}

// `name(` at the start of a line (after blanks), optionally with spaces before '('
bool has_call(std::string_view text, std::string_view name, bool ci) {
    const auto eq = [ci](char a, char b) {
        return ci ? std::tolower(static_cast<unsigned char>(a)) ==
                        std::tolower(static_cast<unsigned char>(b))
                  : a == b;
    };
    for(size_t from = 0; from < text.size();) {
        const auto hit = std::ranges::search(text.substr(from), name, eq);
        if(hit.empty()) return false;
        const size_t at = static_cast<size_t>(hit.begin() - text.begin());
        size_t b = at;
        while(b > 0 && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
        size_t a = at + name.size();
        while(a < text.size() && (text[a] == ' ' || text[a] == '\t')) ++a;
        if((b == 0 || text[b - 1] == '\n') && a < text.size() && text[a] == '(')
            return true;
        from = at + 1;
    }
    return false;
}

std::string_view trim(std::string_view s) {
    while(!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while(!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

std::string_view first_word(std::string_view s) {
    s = trim(s);
    return s.substr(0, s.find_first_of(" \t"));
}

bool one_of(std::string_view w, std::initializer_list<std::string_view> l) {
    return std::ranges::find(l, w) != l.end();
}

// `tool args` when args starts with one of the tool's own subcommands,
// otherwise `tool <dflt> args`; no args at all gives `tool <dflt> <empty_tail>`.
std::string with_sub(std::string_view tool,
                     std::string_view dflt,
                     std::initializer_list<std::string_view> subs,
                     std::string_view args,
                     std::string_view empty_tail = {}) {
    args = trim(args);
    std::string out(tool);
    if(args.empty()) {
        out += ' ';
        out += dflt;
        if(!empty_tail.empty()) (out += ' ') += empty_tail;
        return out;
    }
    if(!one_of(first_word(args), subs)) (out += ' ') += dflt;
    (out += ' ') += args;
    return out;
}

// first of `names` under `root` that contains `marker`
fs::path find_builddir(const fs::path& root,
                       std::initializer_list<const char*> names,
                       const char* marker) {
    for(const char* n : names)
        if(path_exists(root / n / marker)) return root / n;
    return {};
}

std::string join_norm(std::string_view file, const fs::path& base) {
    fs::path p(file);
    if(p.is_relative() && !base.empty()) p = base / p;
    return p.lexically_normal().string();
}

// A file under `root` whose path ends with `rel` (leading ./ and ../ dropped).
std::string find_under(const fs::path& root, const fs::path& rel_in) {
    fs::path rel;
    for(const fs::path& part : rel_in.lexically_normal())
        if(part != ".." && part != "." && part != "/") rel /= part;
    if(rel.empty()) return {};
    const std::string name = rel.filename().string();
    const std::string tail = "/" + rel.generic_string();
    std::error_code ec;
    size_t seen = 0;
    for(fs::recursive_directory_iterator
            it(root, fs::directory_options::skip_permission_denied, ec),
        end;
        !ec && it != end;
        it.increment(ec)) {
        if(++seen > 30000) break;
        std::error_code ec2;
        const std::string fname = it->path().filename().string();
        if(it->is_directory(ec2)) {
            if(fname.starts_with('.') || fname == "node_modules" ||
               fname == "build" || fname == "target")
                it.disable_recursion_pending();
            continue;
        }
        if(fname == name && it->path().generic_string().ends_with(tail))
            return it->path().string();
    }
    return {};
}

}  // namespace

std::string build_resolve_path(std::string_view file,
                               const std::vector<fs::path>& bases,
                               const fs::path& root) {
    for(const fs::path& b : bases) {
        std::string p = join_norm(file, b);
        if(path_exists(p)) return p;
    }
    if(fs::path(file).is_relative()) {
        std::string p = find_under(root, file);
        if(!p.empty()) return p;
    }
    return join_norm(file, bases.empty() ? root : bases.front());
}

std::string build_shell_quote(std::string_view s) {
    std::string out = "'";
    for(const char c : s) {
        if(c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

void build_register(BuildProvider p) {
    for(BuildProvider& q : table())
        if(q.name == p.name) {
            q = std::move(p);
            return;
        }
    table().push_back(std::move(p));
}

std::vector<std::string> build_provider_names() {
    std::vector<std::string> out;
    for(const BuildProvider& p : table()) out.push_back(p.name);
    return out;
}

std::optional<BuildPlan> build_plan(const fs::path& start, std::string_view args) {
    std::error_code ec;
    fs::path dir = fs::absolute(start, ec).lexically_normal();
    if(!dir.has_filename()) dir = dir.parent_path();
    for(;;) {
        const BuildProvider* best = nullptr;
        for(const BuildProvider& p : table()) {
            if(best && p.priority <= best->priority) continue;
            if(!std::ranges::any_of(p.markers,
                                    [&](const std::string& m) {
                                        return path_exists(dir / m);
                                    }))
                continue;
            if(p.detect && !p.detect(BuildRoot{dir})) continue;
            best = &p;
        }
        if(best)
            return BuildPlan{best, BuildRoot{dir}, best->command(BuildRoot{dir}, args)};
        const fs::path parent = dir.parent_path();
        if(parent == dir) return std::nullopt;
        dir = parent;
    }
}

void build_register_builtin() {
    build_register(
        {.name = "meson",
         .markers = {"meson.build"},
         .priority = 50,
         // subdirectory meson.build files have no project() call
         .detect = [](const BuildRoot& r) {
             return has_call(read_head(r.dir / "meson.build"), "project", false);
         },
         .command = [](const BuildRoot& r, std::string_view args) {
             const std::string_view verb = first_word(args);
             const bool sub = one_of(verb,
                                     {"compile", "setup", "configure", "test",
                                      "install", "introspect", "dist",
                                      "reconfigure", "subprojects"});
             const std::string tail =
                 sub ? std::string(trim(args))
                     : std::string("compile") +
                           (trim(args).empty() ? "" : " ") + std::string(trim(args));
             const fs::path bd =
                 find_builddir(r.dir, {"build", "builddir", "_build"}, "build.ninja");
             if(!bd.empty())
                 return BuildCmd{"meson " + tail, bd, {"gcc"}, {r.dir}};
             if(verb == "setup")
                 return BuildCmd{"meson " + tail, r.dir, {"gcc"}, {r.dir / "build"}};
             return BuildCmd{"meson setup build && meson " + tail + " -C build",
                             r.dir,
                             {"gcc"},
                             {r.dir / "build"}};
         }});

    build_register(
        {.name = "cmake",
         .markers = {"CMakeLists.txt"},
         .priority = 40,
         // subdirectory CMakeLists.txt files normally have no project() call
         .detect = [](const BuildRoot& r) {
             return has_call(read_head(r.dir / "CMakeLists.txt"), "project", true);
         },
         .command = [](const BuildRoot& r, std::string_view args) {
             const std::string tail =
                 trim(args).empty() ? "" : " " + std::string(trim(args));
             const fs::path bd = find_builddir(
                 r.dir, {"build", "_build", "builddir"}, "CMakeCache.txt");
             if(!bd.empty())
                 return BuildCmd{"cmake --build " + build_shell_quote(bd.string()) + tail,
                                 r.dir,
                                 {"gcc"},
                                 {bd}};
             return BuildCmd{"cmake -S . -B build && cmake --build build" + tail,
                             r.dir,
                             {"gcc"},
                             {r.dir / "build"}};
         }});

    build_register(
        {.name = "cargo",
         .markers = {"Cargo.toml"},
         .priority = 30,
         .command = [](const BuildRoot& r, std::string_view args) {
             return BuildCmd{with_sub("cargo",
                                      "build",
                                      {"build", "b", "check", "c", "test", "t",
                                       "run", "r", "clippy", "doc", "bench",
                                       "clean", "fmt", "update", "install",
                                       "add", "rustc", "tree"},
                                      args),
                             r.dir,
                             {"rustc"},
                             {}};
         }});

    build_register(
        {.name = "go",
         .markers = {"go.mod"},
         .priority = 30,
         .command = [](const BuildRoot& r, std::string_view args) {
             return BuildCmd{with_sub("go",
                                      "build",
                                      {"build", "test", "vet", "run", "generate",
                                       "install", "fmt", "mod", "get", "clean"},
                                      args,
                                      "./..."),
                             r.dir,
                             {"go"},
                             {}};
         }});

    build_register(
        {.name = "gradle",
         .markers = {"settings.gradle", "settings.gradle.kts", "build.gradle",
                     "build.gradle.kts"},
         .priority = 20,
         // a build.gradle inside a multi-project build is a subproject
         .detect = [](const BuildRoot& r) {
             const auto has_settings = [](const fs::path& d) {
                 return path_exists(d / "settings.gradle") ||
                        path_exists(d / "settings.gradle.kts");
             };
             if(has_settings(r.dir)) return true;
             for(fs::path p = r.dir; p.has_parent_path() && p != p.parent_path();) {
                 p = p.parent_path();
                 if(has_settings(p)) return false;
             }
             return true;
         },
         .command = [](const BuildRoot& r, std::string_view args) {
             const std::string tool = path_exists(r.dir / "gradlew") ? "./gradlew" : "gradle";
             return BuildCmd{tool + " " + (trim(args).empty() ? "build" : std::string(trim(args))),
                             r.dir,
                             {"gcc", "generic"},  // javac prints gcc-style lines
                             {}};
         }});

    build_register(
        {.name = "make",
         .markers = {"Makefile", "makefile", "GNUmakefile"},
         .priority = 10,
         .command = [](const BuildRoot& r, std::string_view args) {
             return BuildCmd{"make" + (trim(args).empty() ? "" : " " + std::string(trim(args))),
                             r.dir,
                             {"gcc"},
                             {}};
         }});
}
