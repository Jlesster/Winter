#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct BuildRoot {
    std::filesystem::path dir;  // directory where a provider matched
};

struct BuildCmd {
    std::string cmd;  // run through bash -c, inside `cwd`
    std::filesystem::path cwd;
    std::vector<std::string> parsers;  // error parsers to try, in order
    // Where relative paths in the tool's output may be relative to, besides
    // `cwd` and the root: meson/ninja print paths relative to the build dir.
    std::vector<std::filesystem::path> resolve_from;
};

// Member order matters: designated initializers must follow it.
struct BuildProvider {
    std::string name;                    // "meson"
    std::vector<std::string> markers;    // files that identify a root
    int priority = 0;                    // tie-break inside one directory
    std::function<bool(const BuildRoot&)> detect;  // optional extra check
    std::function<BuildCmd(const BuildRoot&, std::string_view args)> command;
};

struct BuildPlan {
    const BuildProvider* provider = nullptr;
    BuildRoot root;
    BuildCmd cmd;
};

void build_register(BuildProvider p);  // same name replaces
void build_register_builtin();         // meson cmake cargo go gradle make
std::vector<std::string> build_provider_names();

// Walks up from `start`; the nearest directory that matches a provider wins,
// and priority only breaks ties inside that directory.
std::optional<BuildPlan> build_plan(const std::filesystem::path& start,
                                    std::string_view args);

std::string build_shell_quote(std::string_view s);

// Makes a path printed by a tool absolute: relative paths are tried against
// each base in order and the first that exists wins; failing that, a unique
// file under `root` whose path ends the same way (a go test prints names
// relative to the package, not the module). The result may not exist.
std::string build_resolve_path(std::string_view file,
                               const std::vector<std::filesystem::path>& bases,
                               const std::filesystem::path& root);

void build_register_commands();  // :make :makeprg :mout (make.cpp)
