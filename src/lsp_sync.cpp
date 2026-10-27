#include "lsp_sync.hpp"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include "diag.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "loop.hpp"
#include "lsp_client.hpp"
#include "lsp_config.hpp"
#include "lsp_edit.hpp"
#include "lua_json.hpp"
#include "syntax.hpp"

namespace {
constexpr int kDebounceMs = 150;

struct Doc {
    Buffer* buf;
    LspId server;
    std::string uri, path;
    int version = 1;
    bool utf16 = true;
    int sync = 0;
    bool full_only = false;
    TimerId timer = 0;
};
std::vector<Doc> g_docs;

Doc* find(const Buffer& b) {
    for(Doc& d : g_docs)
        if(d.buf == &b) return &d;
    return nullptr;
}

struct Server {
    std::string name;
    LspId id = 0;
    bool dead = false;
    std::vector<Buffer*> waiting;
};
std::deque<Server> g_servers;

Server* by_id(LspId id) {
    for(Server& s : g_servers)
        if(s.id == id) return &s;
    return nullptr;
}

bool is_waiting(const Buffer& b) {
    for(const Server& s : g_servers)
        if(std::find(s.waiting.begin(), s.waiting.end(), &b) != s.waiting.end())
            return true;
    return false;
}

std::string full_text(const Buffer& b) {
    std::string t = b.pt.text();
    if(b.eol && (b.pt.size() > 0 || !b.orig.empty())) t += '\n';
    return t;
}

int sync_kind(const Json& caps) {
    const Json& s = caps["textDocumentSync"];
    return s.is_int() ? int(s.as_int()) : int(s["change"].as_int(0));
}

void flush(Doc& d) {
    if(d.timer) {
        loop_cancel(d.timer);
        d.timer = 0;
    }
    auto e = d.buf->pt.take_lsp_edit();
    if(!e) return;
    Json change;
    if(d.sync == 2 && !d.full_only && !e->reset) {
        if(auto c = lsp_range_change(d.buf->pt, *e, d.utf16)) change = *c;
        else d.full_only = true;
    }
    if(change.is_null()) change = Json::obj({{"text", full_text(*d.buf)}});
    lsp_notify(
        d.server,
        "textDocument/didChange",
        Json::obj({{"textDocument",
                    Json::obj({{"uri", d.uri}, {"version", ++d.version}})},
                   {"contentChanges", Json::arr({change})}}));
}

std::string abs_path(const Buffer& b) {
    std::error_code ec;
    return std::filesystem::absolute(b.filename, ec)
        .lexically_normal()
        .string();
}

void open_doc(Server& s, Buffer& b, std::string_view lang) {
    const std::string path = abs_path(b);
    const std::string text = full_text(b);
    Doc d{&b, s.id, lsp_uri(path), path};
    d.utf16 = lsp_encoding(s.id) != "utf-8";
    d.sync = sync_kind(lsp_capabilities(s.id));
    d.full_only = !lsp_utf8_valid(text);
    lsp_notify(s.id,
               "textDocument/didOpen",
               Json::obj({{"textDocument",
                           Json::obj({{"uri", d.uri},
                                      {"languageId", lang},
                                      {"version", d.version},
                                      {"text", text}})}}));
    if(d.sync) b.pt.lsp_track(true, d.utf16);
    g_docs.push_back(std::move(d));
}

void on_ready(LspId id) {
    Server* s = by_id(id);
    if(!s) return;
    for(Buffer* b : std::exchange(s->waiting, {}))
        open_doc(*s, *b, Syntax::detect(b->filename));
}

void on_exit(LspId id, std::string_view why) {
    Server* s = by_id(id);
    if(!s) return;
    s->dead = true;
    s->waiting.clear();
    for(Doc& d : g_docs)
        if(d.server == id) {
            if(d.timer) loop_cancel(d.timer);
            d.buf->diags.clear();
            d.buf->pt.lsp_track(false);
        }
    std::erase_if(g_docs, [&](const Doc& d) { return d.server == id; });
    editor_set_status_message(
        "lsp {} exited: {}", s->name, why.substr(0, why.find('\n')));
}

void on_diagnostics(LspId id, const Json& p) {
    const std::string path = lsp_path(p["uri"].as_str());
    for(Doc& d : g_docs) {
        if(d.server != id || d.path != path) continue;
        if(d.buf->pt.lsp_dirty()) return;
        if(p.has("version") && p["version"].as_int() != d.version) return;
        d.buf->sync_anchors();
        d.buf->diags = diag_from_lsp(d.buf->pt, p["diagnostics"], d.utf16);
        return;
    }
}

Json builtin_for(std::string_view name) {
    static std::map<std::string, Json, std::less<>> cache;
    if(const auto it = cache.find(name); it != cache.end()) return it->second;
    const Json probe = lsp_resolve(name, Json::obj());
    const std::string dir(probe["lspconfig"].as_str());
    if(dir.empty()) return Json::obj();
    std::string err;
    auto j =
        lua_load_config_file(dir + "/lsp/" + std::string(name) + ".lua", err);
    if(!j)
        editor_set_status_message(
            "lsp {}: {}", name, err.substr(0, err.find('\n')));
    return cache[std::string(name)] = j ? *j : Json::obj();
}

bool has_filetype(const Json& cfg, std::string_view lang) {
    const Json& ft = cfg["filetypes"];
    if(!ft.is_array()) return false;
    for(const Json& f : ft.items())
        if(f.as_str() == lang) return true;
    return false;
}

Server* ensure_server(const std::string& name, const Json& cfg) {
    for(Server& s : g_servers)
        if(s.name == name) return &s;
    const Json& cmd = cfg["cmd"];
    if(!cmd.is_array() || cmd.size() == 0) {
        editor_set_status_message(
            "lsp {}: no cmd; set the lspconfig dir or pass cmd", name);
        return nullptr;
    }
    LspSpec spec;
    spec.name = name;
    std::error_code ec;
    spec.root = std::filesystem::current_path(ec).string();
    for(const Json& a : cmd.items()) spec.cmd.emplace_back(a.as_str());
    spec.init_options = cfg["init_options"];
    spec.settings = cfg["settings"];
    spec.on_ready = on_ready;
    spec.on_exit = on_exit;
    static bool hooked = false;
    if(!hooked) {
        hooked = true;
        lsp_on("textDocument/publishDiagnostics", on_diagnostics);
    }
    const LspId id = lsp_start(std::move(spec));
    if(!id) {
        editor_set_status_message("lsp {}: failed to start", name);
        return nullptr;
    }
    g_servers.push_back(Server{.name = name, .id = id});
    return &g_servers.back();
}

}  // namespace

void lsp_sync_attach(Buffer& b) {
    if(b.nofile || b.filename.empty() || find(b) || is_waiting(b)) return;
    const std::string_view lang = Syntax::detect(b.filename);
    if(lang.empty()) return;
    for(const std::string& name : lsp_enabled_names()) {
        const Json cfg = lsp_resolve(name, builtin_for(name));
        if(!has_filetype(cfg, lang)) continue;
        Server* s = ensure_server(name, cfg);
        if(!s) continue;
        if(s->dead) return;
        if(lsp_ready(s->id)) open_doc(*s, b, lang);
        else s->waiting.push_back(&b);
        return;
    }
}

void lsp_sync_tick() {
    for(Doc& d : g_docs)
        if(!d.timer && d.buf->pt.lsp_dirty())
            d.timer = loop_after(kDebounceMs, [b = d.buf] {
                if(Doc* d = find(*b)) {
                    d->timer = 0;
                    flush(*d);
                }
            });
}

void lsp_sync_flush(Buffer& b) {
    if(Doc* d = find(b)) flush(*d);
}

void lsp_sync_detach(Buffer& b) {
    for(Server& s : g_servers) std::erase(s.waiting, &b);
    Doc* d = find(b);
    if(!d) return;
    if(d->timer) loop_cancel(d->timer);
    b.pt.lsp_track(false);
    lsp_notify(d->server,
               "textDocument/didClose",
               Json::obj({{"textDocument", Json::obj({{"uri", d->uri}})}}));
    b.diags.clear();
    std::erase_if(g_docs, [&](const Doc& x) { return x.buf == &b; });
}

void lsp_sync_saved(Buffer& b) {
    Doc* d = find(b);
    if(!d) return;
    flush(*d);
    const Json& save = lsp_capabilities(d->server)["textDocumentSync"]["save"];
    if(save.is_null() || (save.is_bool() && !save.as_bool())) return;
    Json p = Json::obj({{"textDocument", Json::obj({{"uri", d->uri}})}});
    if(save["includeText"].as_bool()) p.set("text", full_text(b));
    lsp_notify(d->server, "textDocument/didSave", std::move(p));
}
