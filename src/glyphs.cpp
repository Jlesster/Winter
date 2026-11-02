#include "glyphs.hpp"
#include <cctype>
#include <span>
#include <string_view>
#include <unordered_map>

namespace {
struct DevIcon {
    std::string_view key, glyph;
    uint32_t color;
};
const DevIcon kDevExt[] = {
#include "devicons_ext.inc"
};
const DevIcon kDevName[] = {
#include "devicons_name.inc"
};

std::unordered_map<std::string_view, uint32_t>& dev_colors(bool names) {
    static auto build = [](std::span<const DevIcon> t) {
        std::unordered_map<std::string_view, uint32_t> m;
        for(const DevIcon& d : t) m.emplace(d.key, d.color);
        return m;
    };
    static auto ext = build(kDevExt);
    static auto name = build(kDevName);
    return names ? name : ext;
}

std::unordered_map<std::string, std::string>& ext_icons() {
    static std::unordered_map<std::string, std::string> m = [] {
        std::unordered_map<std::string, std::string> r;
        for(const DevIcon& d : kDevExt)
            r.emplace(d.key, std::string(d.glyph) + ' ');
        for(const DevIcon& d : kDevName)
            r.insert_or_assign(std::string(d.key), std::string(d.glyph) + ' ');
        return r;
    }();
    return m;
}
}  // namespace

std::string_view file_icon(const std::string& name) {
    const Icons& ic = icons();
    if(!opts.nerdicons) return ic.file;
    std::string low(name);
    for(char& c : low)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto& m = ext_icons();
    if(const auto it = m.find(low); it != m.end())
        return it->second;  // whole name
    const size_t dot = low.rfind('.');
    if(dot == std::string::npos || dot == 0) return ic.file;
    const auto it = m.find(low.substr(dot + 1));
    return it == m.end() ? ic.file : std::string_view(it->second);
}

std::optional<uint32_t> file_icon_color(const std::string& name) {
    std::string low(name);
    for(char& c : low)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(const auto it = dev_colors(true).find(low); it != dev_colors(true).end())
        return it->second;
    const size_t dot = low.rfind('.');
    if(dot == std::string::npos || dot == 0) return std::nullopt;
    const auto& m = dev_colors(false);
    const auto it = m.find(std::string_view(low).substr(dot + 1));
    return it == m.end() ? std::nullopt : std::optional<uint32_t>(it->second);
}

void tree_icon_set(std::string_view ext, std::string_view glyph) {
    ext_icons()[std::string(ext)] = std::string(glyph) + " ";
}

const Icons& icons() {
    static const Icons nerd{"\uf07b ", "\uf07c ", "\uf15b "};
    static const Icons plain{"▸", "▾", " "};
    static const Icons no_icons{"", "", ""};
    if(!opts.tree_icons && !opts.nerdicons) return no_icons;
    else return opts.nerdicons ? nerd : plain;
}

const BorderSet& border_set(Border b) {
    static const BorderSet k[] = {
        {"╭", "─", "╮", "│", "│", "╰", "─", "╯"},  // rounded
        {"┌", "─", "┐", "│", "│", "└", "─", "┘"},  // straight
        {"╔", "═", "╗", "║", "║", "╚", "═", "╝"},  // double
        {"▛", "▀", "▜", "▌", "▐", "▙", "▄", "▟"},  // block
    };
    return k[static_cast<int>(b)];
}
