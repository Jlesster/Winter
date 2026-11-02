#include "theme.hpp"
#include <map>
#include <utility>

namespace {
std::map<std::string, std::string, std::less<>> g_link;
std::map<std::string, Style, std::less<>> g_over;
std::vector<uint32_t> g_rainbow;
uint64_t g_rev = 0;
}  // namespace

std::optional<Style> theme_override(std::string_view group) {
    for(int hop = 0; hop < 8; ++hop) {  // hop cap = cycle guard
        if(const auto it = g_over.find(group); it != g_over.end())
            return it->second;
        const auto l = g_link.find(group);
        if(l == g_link.end()) return std::nullopt;
        group = l->second;
    }
    return std::nullopt;
}

Style theme_ui(std::string_view g, Style d) {
    return theme_override(g).value_or(d);
}

uint32_t theme_fg(std::string_view g, uint32_t d) {
    const auto s = theme_override(g);
    return s && s->fg != 0xFFFFFFFF ? s->fg : d;
}

void theme_set(std::string_view group, const Style& st) {
    g_over.insert_or_assign(std::string(group), st);
    ++g_rev;
}

bool theme_unset(std::string_view group) {
    const auto it = g_over.find(group);
    if(it == g_over.end()) return false;
    g_over.erase(it);
    ++g_rev;
    return true;
}

void theme_clear() {
    g_over.clear();
    g_link.clear();
    g_rainbow.clear();
    ++g_rev;
}

void theme_set_rainbow(std::vector<uint32_t> colors) {
    g_rainbow = std::move(colors);
    ++g_rev;
}

std::span<const uint32_t> theme_rainbow() { return g_rainbow; }

const std::vector<std::string_view>& theme_ui_groups() {
    static const std::vector<std::string_view> v = {
        "ui.normal",
        "ui.statusline",
        "ui.message",
        "ui.linenr",
        "ui.linenr.cursor",
        "ui.tilde",
        "ui.visual",
        "ui.search",
        "ui.search.current",
        "ui.search.cursor",
        "ui.cursor",
        "ui.tree.file",
        "ui.tree.dir",
        "ui.tree.hidden",
        "ui.tree.guide",
        "ui.tree.sep",
        "ui.tree.select",
        "ui.menu.select",
        "ui.float",
        "ui.float.border",
        "diag.error",
        "ui.tree.root",
        "ui.tree.open",
        "ui.tree.active",
        "ui.tree.match",
        "ui.tree.symlink",
        "ui.tree.icon.dir",
        "ui.tree.icon.file",
        "ui.tree.git.modified",
        "ui.tree.git.added",
        "ui.tree.git.deleted",
        "ui.tree.git.untracked",
        "ui.tree.git.conflict",
        "diag.warn",
        "diag.info",
        "diag.hint",
        "ui.welcome",
        "ui.ctrl",
        "ui.statusline.normal",
        "ui.statusline.insert",
        "ui.statusline.visual",
        "ui.statusline.vline",
        "ui.statusline.replace",
        "ui.statusline.command",
        "ui.statusline.search",
        "ui.statusline.files",
        "diag.virtual",
        "diag.underline",
        "diag.message",
        "ui.menu",
        "ui.menu.kind",
        "ui.menu.detail",
        "ui.menu.doc",
        "ui.menu.key",
        "ui.menu.disabled",
        "ui.hover",
        "ui.input",
        "ui.input.prompt",
        "ui.input.border",
        "ui.cursorline",
        "ui.colorcolumn",
        "ui.matchparen",
        "ui.trailing",
        "ui.snippet",
        "ui.snippet.active",
        "ui.reference",
        "ui.reference.read",
        "ui.reference.write",
        "ui.subst.match",
        "ui.subst.replace",
    };
    return v;
}

uint64_t theme_rev() { return g_rev; }

void theme_link(std::string_view group, std::string_view target) {
    g_link.insert_or_assign(std::string(group), std::string(target));
    ++g_rev;
}

bool theme_unlink(std::string_view group) {
    const auto it = g_link.find(group);
    if(it == g_link.end()) return false;
    g_link.erase(it);
    ++g_rev;
    return true;
}
