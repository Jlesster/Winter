#include "registry.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

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
    it->second.fn(ctx);
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

void Registry::map(EditorMode m, std::string keys, std::string action) {
    keymaps_[m][std::move(keys)] = std::move(action);
}

bool Registry::unmap(EditorMode m, std::string_view keys) {
    const auto km = keymaps_.find(m);
    if(km == keymaps_.end()) return false;
    const auto it = km->second.find(keys);
    if(it == km->second.end()) return false;
    km->second.erase(it);
    return true;
}

Hit Registry::lookup(EditorMode m, std::string_view keys) const {
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

std::string parse_keys(std::string_view s) {
    std::string out;
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == '<') {
            const size_t close = s.find('>', i);
            if(close != std::string_view::npos && close - i <= 6) {
                std::string name(s.substr(i + 1, close - i - 1));
                std::string low = name;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                char c = 0;
                if(low.size() == 3 && low[0] == 'c' && low[1] == '-')
                    c = static_cast<char>(low[2] & 0x1f);
                else if(low == "esc") c = '\x1b';
                else if(low == "cr") c = '\r';
                else if(low == "tab") c = '\t';
                else if(low == "bs") c = 127;
                else if(low == "space") c = ' ';
                else if(low == "lt") c = '<';
                if(c) {
                    out += c;
                    i = close;
                    continue;
                }
            }
        }
        out += s[i];  // plain key (or a '<' that isn't a known name)
    }
    return out;
}
