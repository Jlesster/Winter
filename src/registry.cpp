#include "registry.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include "keys.hpp"

Registry& registry() {
    static Registry r;
    return r;
}

void Registry::add_action(std::string name, Action fn, bool wants_char) {
    actions_[std::move(name)] = Entry{std::move(fn), wants_char};
}

bool Registry::run_action(std::string_view name, Ctx& ctx) const {
    const auto it = actions_.find(name);
    if(it == actions_.end()) return false;
    const Action fn = it->second.fn;
    fn(ctx);
    return true;
}

bool Registry::remove_action(std::string_view name) {
    const auto it = actions_.find(name);
    if(it == actions_.end()) return false;
    actions_.erase(it);
    return true;
}

bool Registry::wants_char(std::string_view name) const {
    const auto it = actions_.find(name);
    return it != actions_.end() && it->second.wants_char;
}

std::vector<std::string> Registry::action_names() const {
    std::vector<std::string> out;
    for(const auto& [name, entry] : actions_) out.push_back(name);
    return out;
}

void Registry::add_motion(std::string name, motion::Fn fn) {
    motions_[std::move(name)] = fn;
}

motion::Fn Registry::motion(std::string_view name) const {
    const auto it = motions_.find(name);
    return it == motions_.end() ? nullptr : it->second;
}

void Registry::map(EditorMode m, KeySeq keys, std::string action) {
    if(keys.empty()) return;  // a bad notation maps nothing
    keymaps_[m][std::move(keys)] = std::move(action);
}

void Registry::map(EditorMode m, const char* notation, std::string action) {
    map(m, parse_keys(notation), std::move(action));
}

bool Registry::unmap(EditorMode m, std::u32string_view keys) {
    const auto km = keymaps_.find(m);
    if(km == keymaps_.end()) return false;
    const auto it = km->second.find(keys);
    if(it == km->second.end()) return false;
    km->second.erase(it);
    return true;
}

Hit Registry::lookup(EditorMode m, std::u32string_view keys) const {
    const auto km = keymaps_.find(m);
    if(km == keymaps_.end() || keys.empty()) return {};
    const auto& map = km->second;

    // the map is sorted, so everything starting with `keys` sits right after it
    const auto it = map.lower_bound(keys);
    const bool exact = it != map.end() && it->first == keys;
    const auto after = exact ? std::next(it) : it;
    const bool longer = after != map.end() && after->first.starts_with(keys);

    if(exact) return {Lookup::Exact, it->second, longer};
    if(longer) return {Lookup::Prefix, {}, true};
    return {};
}

namespace {
struct NamedKey {
    std::string_view name;
    int key;
};
// canonical names first: format_keys() prints the first match
constexpr NamedKey kNamed[] = {
    {"Esc", 27},          {"CR", '\r'},
    {"Tab", '\t'},        {"BS", BACKSPACE},
    {"Space", ' '},       {"lt", '<'},
    {"Up", ARROW_UP},     {"Down", ARROW_DOWN},
    {"Left", ARROW_LEFT}, {"Right", ARROW_RIGHT},
    {"Home", HOME},       {"End", END},
    {"PageUp", PAGE_UP},  {"PageDown", PAGE_DOWN},
    {"Del", DELETE},      {"C-BS", CTRL_BACKSPACE},
    {"Enter", '\r'},      {"Return", '\r'},
    {"Delete", DELETE},
};

bool iequals(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
    });
}

// "C-S-Left" -> key. false = not a key name (caller types it literally);
// *err is set when it IS a key but can't exist.
bool parse_one(std::string_view name, int& out, const char** err) {
    int mods = 0;
    while(name.size() > 2 && name[1] == '-') {
        switch(std::tolower(static_cast<unsigned char>(name[0]))) {
            case 'c': mods |= MOD_CTRL; break;
            case 'a':
            case 'm': mods |= MOD_ALT; break;
            case 's': mods |= MOD_SHIFT; break;
            default: return false;
        }
        name.remove_prefix(2);
    }
    int base = -1;
    if(name.size() == 1) {
        if(!mods) return false;  // "<x>" is just the text <x>
        base = static_cast<unsigned char>(name[0]);
    } else {
        for(const auto& n : kNamed)
            if(iequals(n.name, name)) {
                base = n.key;
                break;
            }
    }
    if(base < 0) return false;
    const int k = key_normalize(base, mods);
    if(k < 0) {
        if(err) *err = "that modifier combination can't be typed in a terminal";
        return false;
    }
    out = k;
    return true;
}
}  // namespace

KeySeq parse_keys(std::string_view s, const char** err) {
    if(err) *err = nullptr;
    KeySeq out;
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == '<') {
            const size_t close = s.find('>', i);
            if(close != std::string_view::npos && close - i <= 16) {
                int k;
                if(parse_one(s.substr(i + 1, close - i - 1), k, err)) {
                    out += static_cast<char32_t>(k);
                    i = close;
                    continue;
                }
                if(err && *err) return {};  // known key, impossible combo
            }
        }
        // one element per byte, like the terminal delivers them (UTF-8 = N
        // keys)
        out += static_cast<char32_t>(static_cast<unsigned char>(s[i]));
    }
    return out;
}

std::string format_keys(const KeySeq& keys) {
    std::string out;
    for(const char32_t ch : keys) {
        const int base = key_base(static_cast<int>(ch));
        const int mods = key_mods(static_cast<int>(ch));
        std::string name;  // non-empty -> needs <...>
        for(const auto& n : kNamed)
            if(n.key == base) {
                name = n.name;
                break;
            }
        if(name.empty() && base == 0) name = "C-Space";
        else if(name.empty() && base < 32)
            name = std::string("C-") + static_cast<char>('a' + base - 1);
        if(name.empty() && !mods) {
            out += static_cast<char>(base);
            continue;
        }
        if(name.empty()) name = std::string(1, static_cast<char>(base));
        std::string pre;
        if(mods & MOD_CTRL) pre += "C-";
        if(mods & MOD_ALT) pre += "A-";
        if(mods & MOD_SHIFT) pre += "S-";
        out += '<';
        out += pre;
        out += name;
        out += '>';
    }
    return out;
}

void Registry::add_operator(std::string name, Operator fn) {
    ops_[std::move(name)] = std::move(fn);
}

const Operator* Registry::op(std::string_view name) const {
    const auto it = ops_.find(name);
    return it == ops_.end() ? nullptr : &it->second;
}

void Registry::add_object(std::string name, ObjectFn fn) {
    objects_[std::move(name)] = fn;
}

ObjectFn Registry::object(std::string_view name) const {
    const auto it = objects_.find(name);
    return it == objects_.end() ? nullptr : it->second;
}

namespace {
Registry g_builtin;
bool g_have_builtin = false;
}  // namespace

void registry_snapshot() {
    g_builtin = registry();
    g_have_builtin = true;
}

void registry_restore() {
    if(g_have_builtin) registry() = g_builtin;
}
