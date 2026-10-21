#include "options.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <sstream>

Options opts;

namespace {

struct Opt {
    const char* name;
    const char* alias;  // "" = none
    bool is_bool;
    void* ptr;   // int* or bool*
    int lo, hi;  // allowed range for ints
};

Opt kTable[] = {
    {"tabstop", "ts", false, &opts.tab_stop, 1, 16},
    {"shiftwidth", "sw", false, &opts.shift_width, 1, 16},
    {"expandtab", "et", true, &opts.expand_tab, 0, 1},
    {"quit_times", "", false, &opts.quit_times, 0, 99},
    {"cursor_normal", "", false, &opts.cursor_normal, 0, 6},
    {"cursor_insert", "", false, &opts.cursor_insert, 0, 6},
    {"cursor_visual", "", false, &opts.cursor_visual, 0, 6},
    {"number", "nu", true, &opts.number, 0, 1},
    {"relativenumber", "rnu", true, &opts.relative_number, 0, 1},
    {"numberwidth", "nuw", false, &opts.number_width, 1, 10},
};

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

std::string show(const Opt& o) {
    if(o.is_bool) return *static_cast<bool*>(o.ptr) ? "true" : "false";
    return std::to_string(*static_cast<int*>(o.ptr));
}

}  // namespace

OptResult set_option(std::string_view name, std::string_view value) {
    Opt* o = find(name);
    if(!o) return OptResult::Unknown;
    if(o->is_bool) {
        const auto b = parse_bool(value);
        if(!b) return OptResult::BadValue;
        *static_cast<bool*>(o->ptr) = *b;
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
            if(o && o->is_bool) {
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
            if(o && o->is_bool) set_option(t, "true");
            else if(neg && neg->is_bool) set_option(t.substr(2), "false");
            else if(o) msgs.push_back(std::format("{}={}", t, show(*o)));
            else msgs.push_back(std::format("Unknown option: {}", t));
        }
    }
    std::string out;
    for(size_t i = 0; i < msgs.size(); ++i) out += (i ? ", " : "") + msgs[i];
    return out;
}
