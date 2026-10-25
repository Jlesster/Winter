#include "lsp_config.hpp"
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace {
struct Entry {
    std::string name;
    Json opts = Json::obj();
    bool enabled = true;
};

std::vector<Entry> g_servers;
Json g_defaults = Json::obj();

Entry* find(std::string_view name) {
    for(Entry& e : g_servers)
        if(e.name == name) return &e;
    return nullptr;
}

Json merge(const Json& base, const Json& over) {
    if(!base.is_object() || !over.is_object()) return over;
    Json::Object out = base.members();
    for(const auto& [k, v] : over.members()) {
        auto it =
            std::find_if(out.begin(), out.end(), [&](const Json::Member& m) {
                return m.first == k;
            });
        if(it == out.end()) out.emplace_back(k, v);
        else it->second = merge(it->second, v);
    }
    return Json::from_object(std::move(out));
}

Json without(const Json& ob, std::string_view key) {
    Json::Object out;
    for(const auto& m : ob.members())
        if(m.first != key) out.push_back(m);
    return Json::from_object(std::move(out));
}

}  // namespace

std::string lsp_configure(std::string_view name, Json opts) {
    if(name.empty()) return "server name is empty";
    if(!opts.is_object()) return "opts must be a table of settings, not a list";
    std::optional<bool> enabled;
    if(opts.has("enabled")) {
        if(!opts["enabled"].is_bool()) return "'enabled' must be true or false";
        enabled = opts["enabled"].as_bool();
        opts = without(opts, "enabled");
    }
    if(name == "*") {
        if(enabled) return "'enabled' has no meaning for \"*\"";
        g_defaults = merge(g_defaults, opts);
        return {};
    }
    Entry* e = find(name);
    if(!e) {
        g_servers.push_back(Entry{std::string(name)});
        e = &g_servers.back();
    }
    e->opts = merge(e->opts, opts);
    if(enabled) e->enabled = *enabled;
    return {};
}

bool lsp_enabled(std::string_view name) {
    const Entry* e = find(name);
    return e && e->enabled;
}

std::vector<std::string> lsp_enabled_names() {
    std::vector<std::string> out;
    for(const Entry& e : g_servers)
        if(e.enabled) out.push_back(e.name);
    return out;
}

Json lsp_resolve(std::string_view name, const Json& builtin) {
    Json r = merge(g_defaults, builtin);
    if(const Entry* e = find(name)) r = merge(r, e->opts);
    return r;
}
