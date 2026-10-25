#include "options.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <sstream>
#include <string_view>

Options opts;

namespace {

enum class Kind { Int, Bool, CursorSpec };

struct Opt {
    const char* name;
    const char* alias;  // "" = none
    Kind kind;
    void* ptr;   // int* or bool*
    int lo, hi;  // allowed range for ints
};

Opt kTable[] = {
    {"tabstop", "ts", Kind::Int, &opts.tab_stop, 1, 16},
    {"shiftwidth", "sw", Kind::Int, &opts.shift_width, 1, 16},
    {"expandtab", "et", Kind::Bool, &opts.expand_tab, 0, 1},
    {"quit_times", "", Kind::Int, &opts.quit_times, 0, 99},
    {"cursor", "", Kind::CursorSpec, nullptr, 0, 0},
    {"cursor_inverse", "", Kind::Bool, &opts.cursor_inverse, 0, 1},
    {"number", "nu", Kind::Bool, &opts.number, 0, 1},
    {"relativenumber", "rnu", Kind::Bool, &opts.relative_number, 0, 1},
    {"numberwidth", "nuw", Kind::Int, &opts.number_width, 1, 10},
    {"rainbow", "rb", Kind::Bool, &opts.rainbow, 0, 1},
    {"diagnostics", "diag", Kind::Bool, &opts.diagnostics, 0, 1},
    {"diag_virtual_text", "", Kind::Bool, &opts.diag_virtual_text, 0, 1},
    {"diag_undercurl", "", Kind::Bool, &opts.diag_undercurl, 0, 1},
    {"diag_severity", "", Kind::Int, &opts.diag_severity, 1, 4},
    {"scrolloff", "so", Kind::Int, &opts.scrolloff, 0, 999},
    {"scrolleof", "", Kind::Bool, &opts.scrolleof, 0, 1},
    {"history", "hi", Kind::Int, &opts.history, 0, 10000},
    {"remember_cursor", "", Kind::Bool, &opts.remember_cursor, 0, 1},
};

std::optional<CursorStyle> parse_cursor(std::string_view v) {
    CursorStyle c;
    if(v.ends_with("-blink")) {
        c.blink = true;
        v.remove_suffix(6);
    }
    if(v == "default") c.shape = CursorShape::Default;
    else if(v == "block") c.shape = CursorShape::Block;
    else if(v == "underline") c.shape = CursorShape::Underline;
    else if(v == "bar" || v == "beam") c.shape = CursorShape::Bar;
    else return std::nullopt;
    if(c.shape == CursorShape::Default && c.blink) return std::nullopt;
    return c;
}

Opt* find(std::string_view name) {
    for(Opt& o : kTable)
        if(name == o.name || (*o.alias && name == o.alias)) return &o;
    return nullptr;
}

std::optional<bool> parse_bool(std::string_view v) {
    if(v == "true" || v == "1" || v == "on" || v == "yes") return true;
    if(v == "false" || v == "0" || v == "off" || v == "no") return false;
    return std::nullopt;
}

std::string show_cursor(const CursorStyle& c) {
    static constexpr const char* names[] = {
        "default", "block", "underline", "bar"};
    return std::string(names[static_cast<int>(c.shape)]) +
           (c.blink ? "-blink" : "");
}

struct CursorSet {
    CursorStyle n, v, i;
};

std::string_view trim(std::string_view s) {
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::optional<CursorSet> parse_cursor_spec(std::string_view spec) {
    CursorSet out{opts.cursor_normal, opts.cursor_visual, opts.cursor_insert};
    std::string low(spec);
    std::ranges::transform(low, low.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string_view rest = low;
    while(!rest.empty()) {
        const size_t semi = rest.find(';');
        const std::string_view entry = trim(rest.substr(0, semi));
        rest = semi == std::string_view::npos ? std::string_view{}
                                              : rest.substr(semi + 1);
        if(entry.empty()) continue;
        const size_t colon = entry.find(':');
        if(colon == std::string_view::npos) return std::nullopt;
        const std::string_view mode = trim(entry.substr(0, colon));
        const auto style = parse_cursor(trim(entry.substr(colon + 1)));
        if(!style) return std::nullopt;
        if(mode == "n") out.n = *style;
        else if(mode == "v") out.v = *style;
        else if(mode == "i") out.i = *style;
        else return std::nullopt;
    }
    return out;
}

std::string show_cursor_spec() {
    return "n:" + show_cursor(opts.cursor_normal) +
           ";v:" + show_cursor(opts.cursor_visual) +
           ";i:" + show_cursor(opts.cursor_insert);
}

std::string show(const Opt& o) {
    switch(o.kind) {
        case Kind::Bool: return *static_cast<bool*>(o.ptr) ? "true" : "false";
        case Kind::CursorSpec: return show_cursor_spec();
        case Kind::Int: break;
    }
    return std::to_string(*static_cast<int*>(o.ptr));
}

}  // namespace

OptResult set_option(std::string_view name, std::string_view value) {
    Opt* o = find(name);
    if(!o) return OptResult::Unknown;
    if(o->kind == Kind::Bool) {
        const auto b = parse_bool(value);
        if(!b) return OptResult::BadValue;
        *static_cast<bool*>(o->ptr) = *b;
        return OptResult::Ok;
    }
    if(o->kind == Kind::CursorSpec) {
        const auto c = parse_cursor_spec(value);
        if(!c) return OptResult::BadValue;
        opts.cursor_normal = c->n;
        opts.cursor_visual = c->v;
        opts.cursor_insert = c->i;
        return OptResult::Ok;
    }
    int n = 0;
    const auto [end, ec] =
        std::from_chars(value.data(), value.data() + value.size(), n);
    if(ec != std::errc{} || end != value.data() + value.size() || n < o->lo ||
       n > o->hi)
        return OptResult::BadValue;
    *static_cast<int*>(o->ptr) = n;
    return OptResult::Ok;
}

std::optional<std::string> get_option(std::string_view name) {
    const Opt* o = find(name);
    if(!o) return std::nullopt;
    return show(*o);
}

std::vector<std::string_view> option_names() {
    std::vector<std::string_view> out;
    for(const Opt& o : kTable) out.push_back(o.name);
    return out;
}

std::string apply_set_command(std::string_view args) {
    std::vector<std::string> msgs;
    std::istringstream in{std::string(args)};
    std::string tok;
    while(in >> tok) {
        std::string_view t = tok;
        if(t.ends_with('!')) {  // number
            t.remove_suffix(1);
            Opt* o = find(t);
            if(o && o->kind == Kind::Bool) {
                bool* b = static_cast<bool*>(o->ptr);
                *b = !*b;
            } else {
                msgs.push_back(std::format("Not a toggle option: {}", t));
            }
        } else if(t.ends_with('?')) {  // query
            t.remove_suffix(1);
            if(const auto v = get_option(t))
                msgs.push_back(std::format("{}={}", t, *v));
            else msgs.push_back(std::format("Unknown option: {}", t));
        } else if(const auto eq = t.find('='); eq != std::string_view::npos) {
            const auto name = t.substr(0, eq), value = t.substr(eq + 1);
            switch(set_option(name, value)) {
                case OptResult::Ok: break;
                case OptResult::Unknown:
                    msgs.push_back(std::format("Unknown option: {}", name));
                    break;
                case OptResult::BadValue:
                    msgs.push_back(
                        std::format("Invalid value for {}: {}", name, value));
                    break;
            }
        } else {  // bare word: "expandtab", "noexpandtab", or "tabstop" (show
                  // it)
            const Opt* o = find(t);
            const Opt* neg = t.starts_with("no") ? find(t.substr(2)) : nullptr;
            if(o && o->kind == Kind::Bool) set_option(t, "true");
            else if(neg && neg->kind == Kind::Bool)
                set_option(t.substr(2), "false");
            else if(o) msgs.push_back(std::format("{}={}", t, show(*o)));
            else msgs.push_back(std::format("Unknown option: {}", t));
        }
    }
    std::string out;
    for(size_t i = 0; i < msgs.size(); ++i) out += (i ? ", " : "") + msgs[i];
    return out;
}
