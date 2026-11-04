#include "lsp_sync.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
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
#include "options.hpp"
#include "syntax.hpp"
#include "theme.hpp"
#include "utf8.hpp"
#include "virt.hpp"

namespace {
constexpr int kDebounceMs = 150;

struct SemTok {
    size_t start, end, type;
};

struct SemStats {
    int sent = 0, ok = 0, stale = 0, empty = 0, err = 0;
    int req_ver = 0, got_ver = 0;
    double ms = 0;
    size_t raw = 0, applied = 0, unmapped = 0;
    std::string last_err;
    std::vector<size_t> counts;  // tokens per legend type id
    std::vector<SemTok> toks;    // every token as of got_ver, sorted by start
};

struct Doc {
    Buffer* buf;
    LspId server;
    std::string uri, path;
    int version = 1;
    bool utf16 = true;
    int sync = 0;
    bool full_only = false;
    TimerId timer = 0;
    SemStats sem;
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
    bool restart = false;
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

constexpr int kPrioSemantic = 5;
bool g_sem_debug = false;

std::optional<Style> token_style(std::string_view type) {
    if(const auto u = theme_override("lsp." + std::string(type))) return u;
    static constexpr std::pair<std::string_view, std::string_view> k[] = {
        {"namespace", "namespace"},
        {"type", "type"},
        {"class", "type"},
        {"struct", "type"},
        {"enum", "type"},
        {"interface", "type"},
        {"typeParameter", "type"},
        {"concept", "type"},
        {"parameter", "variable.parameter"},
        {"property", "property"},
        {"enumMember", "constant"},
        {"function", "function"},
        {"method", "method"},
        {"macro", "define"},
        {"decorator", "attribute"},
        {"label", "label"},
    };
    for(const auto& [name, cap] : k)
        if(name == type) return syntax_theme(cap);
    return std::nullopt;
}

struct Legend {
    bool ok = false, built = false;
    uint64_t rev = 0;
    std::vector<std::string> names;
    std::vector<std::optional<Style>> style;
    std::vector<std::string> mods;
    mutable std::unordered_map<uint64_t, std::optional<Style>> combo;
};

const Legend* legend_for(LspId id) {
    static std::map<LspId, Legend> cache;
    Legend& lg = cache[id];
    if(!lg.built || lg.rev != theme_rev()) {
        lg = Legend{};
        lg.built = true;
        lg.rev = theme_rev();
        const Json& p = lsp_capabilities(id)["semanticTokensProvider"];
        const Json& full = p["full"];
        lg.ok = p.is_object() &&
                (full.is_bool() ? full.as_bool() : full.is_object());
        const Json& mods = p["legend"]["tokenModifiers"];
        if(lg.ok && mods.is_array())
            for(const Json& m : mods.items()) lg.mods.emplace_back(m.as_str());
        const Json& types = p["legend"]["tokenTypes"];
        if(lg.ok && types.is_array())
            for(const Json& t : types.items()) {
                lg.names.emplace_back(t.as_str());
                lg.style.push_back(token_style(t.as_str()));
            }
    }
    return lg.ok ? &lg : nullptr;
}

struct LineCur {
    const PieceTable& pt;
    bool utf16;
    size_t line = static_cast<size_t>(-1), base = 0, i = 0, units = 0;
    std::string text;

    void seek(size_t l) {
        if(l == line) return;
        line = l;
        base = pt.line_start(l);
        text = pt.line_text(l);
        i = units = 0;
    }
    size_t at(size_t col) {
        if(!utf16) return base + std::min(col, text.size());
        if(col < units) i = units = 0;  // overlapping token: rewind
        while(i < text.size() && units < col) {
            const Decoded d = utf8_decode(text, i);
            units += d.cp >= 0x10000 ? 2 : 1;
            i += d.len;
        }
        return base + i;
    }
};

std::optional<Style> token_style_mods(const Legend& lg, size_t ty,
                                      uint64_t bits) {
    const uint64_t key = (uint64_t(ty) << 32) | (bits & 0xFFFFFFFF);
    if(const auto it = lg.combo.find(key); it != lg.combo.end())
        return it->second;
    std::optional<Style> out = lg.style[ty];
    for(size_t i = 0; i < lg.mods.size() && i < 32; ++i) {
        if(!((bits >> i) & 1)) continue;
        for(const std::string& g :
            {"lsp." + lg.names[ty] + "." + lg.mods[i], "lsp.mod." + lg.mods[i]})
            if(const auto m = theme_override(g)) {
                if(!out) out = Style{};
                style_patch(*out, *m);
            }
    }
    return lg.combo.emplace(key, out).first->second;
}

void apply_tokens(Doc& d, const Legend& lg, const Json& data) {
    Buffer& b = *d.buf;
    SemStats& st = d.sem;
    b.sync_anchors();  // flush pending deco shifts BEFORE adding new spans
    const NsId ns = deco_ns("lsp.semantic");
    b.decos.clear_ns(ns);
    st.raw = st.applied = st.unmapped = 0;
    st.counts.assign(lg.style.size(), 0);
    st.toks.clear();
    if(!data.is_array()) return;
    std::vector<int64_t> v;
    for(const Json& n : data.items()) v.push_back(n.as_int());

    LineCur cur{b.pt, d.utf16};
    size_t line = 0, col = 0;
    for(size_t k = 0; k + 4 < v.size(); k += 5) {
        // [deltaLine, deltaStart, length, type, modifiers]
        if(v[k] > 0) {
            line += static_cast<size_t>(v[k]);
            col = static_cast<size_t>(v[k + 1]);
        } else {
            col += static_cast<size_t>(v[k + 1]);
        }
        if(line >= b.pt.line_count()) break;
        ++st.raw;
        const auto ty = static_cast<size_t>(v[k + 3]);
        if(ty >= lg.style.size()) continue;
        ++st.counts[ty];
        cur.seek(line);
        const size_t s = cur.at(col);
        const size_t e = cur.at(col + static_cast<size_t>(v[k + 2]));
        if(e <= s) continue;
        st.toks.push_back({s, e, ty});
        const auto base =
            token_style_mods(lg, ty, static_cast<uint64_t>(v[k + 4]));
        if(!base) {
            ++st.unmapped;
            continue;
        }
        Style sty = *base;
        if(g_sem_debug)
            sty.attrs = static_cast<uint16_t>(sty.attrs | ATTR_UNDERLINE);
        b.decos.add_span({s, e, ns, kPrioSemantic, sty});
        ++st.applied;
    }
}

void request_tokens(Doc& d) {
    Buffer* b = d.buf;
    if(!opts.semantic_tokens) {
        b->decos.clear_ns(deco_ns("lsp.semantic"));
        return;
    }
    if(!legend_for(d.server)) return;
    const int ver = d.version;
    d.sem.req_ver = ver;
    ++d.sem.sent;
    const auto t0 = std::chrono::steady_clock::now();
    lsp_request(d.server,
                "textDocument/semanticTokens/full",
                Json::obj({{"textDocument", Json::obj({{"uri", d.uri}})}}),
                [b, ver, t0](const Json& result, const Json& error) {
                    Doc* d = find(*b);  // the buffer may have been closed
                    if(!d) return;
                    SemStats& st = d->sem;
                    // stale: text moved on since we asked; the next flush
                    // re-asks
                    if(d->version != ver || b->pt.lsp_dirty()) {
                        ++st.stale;
                        return;
                    }
                    if(!error.is_null()) {
                        ++st.err;
                        st.last_err = std::string(error["message"].as_str("?"));
                        return;
                    }
                    if(!result.is_object()) {
                        ++st.empty;
                        return;
                    }
                    st.ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - t0)
                                .count();
                    st.got_ver = ver;
                    ++st.ok;
                    if(const Legend* lg = legend_for(d->server))
                        apply_tokens(*d, *lg, result["data"]);
                });
}

constexpr int kRefDelayMs = 120;  // idle time before asking the server
constexpr int kPrioRef = 4;       // under semantic tokens (5) and search (10)

struct RefState {
    Buffer* buf = nullptr;
    uint64_t rev = 0;  // pt.revision() the highlights were asked for
    size_t pos = 0;    // cursor offset they were asked for
    TimerId timer = 0;
    std::vector<std::pair<size_t, size_t>> ranges;  // highlighted, as of rev
};
RefState g_ref;

size_t col_units(const PieceTable& pt, size_t off, bool utf16) {
    const size_t ls = pt.line_start(pt.line_of(off));
    const std::string t = pt.text(ls, off - ls);
    if(!utf16) return t.size();
    size_t u = 0;
    for(size_t i = 0; i < t.size();) {
        const Decoded d = utf8_decode(t, i);
        u += d.cp >= 0x10000 ? 2 : 1;
        i += d.len;
    }
    return u;
}

bool ref_supported(LspId id) {
    const Json& p = lsp_capabilities(id)["documentHighlightProvider"];
    return p.is_bool() ? p.as_bool() : p.is_object();
}

void ref_clear(Buffer* b) {
    b->decos.clear_ns(deco_ns("lsp.refs"));
    g_ref.ranges.clear();
}

void ref_request(Buffer* b, uint64_t rev, size_t cur) {
    const auto doc = lsp_sync_doc(*b, true);  // flush pending edits first
    if(!doc || !ref_supported(doc->server)) return;
    lsp_request(
        doc->server,
        "textDocument/documentHighlight",
        Json::obj(
            {{"textDocument", Json::obj({{"uri", doc->uri}})},
             {"position",
              Json::obj({{"line", b->pt.line_of(cur)},
                         {"character", col_units(b->pt, cur, doc->utf16)}})}}),
        [b, rev, cur, utf16 = doc->utf16](const Json& result,
                                          const Json& error) {
            // stale: the cursor or the text moved on, or the buffer changed
            if(g_ref.buf != b || g_ref.rev != rev || g_ref.pos != cur) return;
            b->sync_anchors();  // flush pending deco shifts BEFORE adding spans
            ref_clear(b);
            if(!error.is_null() || !result.is_array()) return;

            const Style base = theme_ui("ui.reference", Style{.bg = 0x45475a});
            Style wdef = base;
            wdef.attrs = static_cast<uint16_t>(wdef.attrs | ATTR_UNDERLINE);
            const Style rd = theme_ui("ui.reference.read", base);
            const Style wr = theme_ui("ui.reference.write", wdef);
            const NsId ns = deco_ns("lsp.refs");
            for(const Json& h : result.items()) {
                const Json& r = h["range"];
                const size_t s = lsp_offset(b->pt,
                                            r["start"]["line"].as_int(),
                                            r["start"]["character"].as_int(),
                                            utf16);
                const size_t e = lsp_offset(b->pt,
                                            r["end"]["line"].as_int(),
                                            r["end"]["character"].as_int(),
                                            utf16);
                if(e <= s) continue;
                const int64_t kind =
                    h["kind"].as_int(1);  // 1 text 2 read 3 write
                b->decos.add_span({s,
                                   e,
                                   ns,
                                   kPrioRef,
                                   kind == 3   ? wr
                                   : kind == 2 ? rd
                                               : base});
                g_ref.ranges.emplace_back(s, e);
            }
        });
}

// Called every frame. Keeps the highlights in step with the cursor.
void ref_tick() {
    Buffer* b = &E.buf();
    const bool want =
        opts.hl_reference && (E.mode == NORMAL || E.mode == OPPEND);
    Doc* d = want ? find(*b) : nullptr;
    if(!d || !ref_supported(d->server)) {
        if(g_ref.buf) {  // switched off, left insert/normal, or no server here
            if(g_ref.timer) loop_cancel(g_ref.timer);
            ref_clear(g_ref.buf);
            g_ref = RefState{};
        }
        return;
    }
    const size_t cur = cursor_offset();
    const uint64_t rev = b->pt.revision();
    if(g_ref.buf == b && g_ref.rev == rev && g_ref.pos == cur) return;
    if(g_ref.buf && g_ref.buf != b) {  // moved to another buffer
        if(g_ref.timer) loop_cancel(g_ref.timer);
        ref_clear(g_ref.buf);
        g_ref = RefState{};
    }
    const bool inside = g_ref.buf == b && g_ref.rev == rev &&
                        std::ranges::any_of(g_ref.ranges, [&](const auto& r) {
                            return cur >= r.first && cur <= r.second;
                        });
    if(g_ref.timer) {
        loop_cancel(g_ref.timer);
        g_ref.timer = 0;
    }
    if(!inside) ref_clear(b);
    g_ref.buf = b;
    g_ref.rev = rev;
    g_ref.pos = cur;
    if(inside) return;
    g_ref.timer = loop_after(kRefDelayMs, [b, rev, cur] {
        g_ref.timer = 0;
        if(g_ref.buf == b && g_ref.rev == rev && g_ref.pos == cur)
            ref_request(b, rev, cur);
    });
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
    request_tokens(d);
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
    request_tokens(g_docs.back());
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
            d.buf->decos.clear_ns(deco_ns("lsp.semantic"));
            d.buf->decos.clear_ns(deco_ns("lsp.refs"));
            d.buf->pt.lsp_track(false);
        }
    if(s->restart) {
        editor_set_status_message("lsp {}: restarting", s->name);
        std::erase_if(g_servers, [id](const Server& x) { return x.id == id; });
        loop_after(0, [] {
            for(auto& b : E.buffers) lsp_sync_attach(*b);
        });
        return;
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

std::string lsp_sync_server_of(Buffer& b) {
    if(const Doc* d = find(b))
        if(const Server* s = by_id(d->server)) return s->name;
    for(const Server& s : g_servers)
        if(std::ranges::find(s.waiting, &b) != s.waiting.end()) return s.name;
    return {};
}

std::string lsp_sync_server_start(std::string_view name) {
    if(!lsp_enabled(name))
        return std::format("lsp {}: not in the registry, or disabled", name);
    for(const Server& s : g_servers)
        if(s.name == name && !s.dead)
            return std::format("lsp {}: already running", name);
    std::erase_if(g_servers, [&](const Server& s) { return s.name == name; });
    const Json cfg = lsp_resolve(name, builtin_for(name));
    if(!ensure_server(std::string(name), cfg)) return {};
    for(auto& b : E.buffers) lsp_sync_attach(*b);
    return std::format("lsp {}: starting", name);
}

std::string lsp_sync_server_stop(std::string_view name) {
    for(Server& s : g_servers)
        if(s.name == name && !s.dead) {
            s.restart = false;
            lsp_stop(s.id);
            return std::format("lsp {}: stopping", name);
        }
    return std::format("lsp {}: not running", name);
}

std::string lsp_sync_server_restart(std::string_view name) {
    for(Server& s : g_servers)
        if(s.name == name && !s.dead) {
            s.restart = true;
            lsp_stop(s.id);
            return std::format("lsp {}: restarting", name);
        }
    return lsp_sync_server_start(name);
}

void lsp_sync_tick() {
    ref_tick();
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
    b.decos.clear_ns(deco_ns("lsp.semantic"));
    b.decos.clear_ns(deco_ns("lsp.refs"));
    if(g_ref.buf == &b) {
        if(g_ref.timer) loop_cancel(g_ref.timer);
        g_ref = RefState{};
    }
    std::erase_if(g_docs, [&](const Doc& x) { return x.buf == &b; });
}

std::optional<int> lsp_sync_version(Buffer& b) {
    Doc* d = find(b);
    if(!d) return std::nullopt;
    flush(*d);
    return d->version;
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

std::optional<LspDoc> lsp_sync_doc(Buffer& b, bool flush_pending) {
    Doc* d = find(b);
    if(!d || !lsp_ready(d->server)) return std::nullopt;
    if(flush_pending) flush(*d);
    const Server* sv = by_id(d->server);
    return LspDoc{d->server, d->uri, d->utf16, sv ? sv->name : std::string{}};
}

bool lsp_sync_semantic_debug(Buffer& b) {
    g_sem_debug = !g_sem_debug;
    if(Doc* d = find(b)) request_tokens(*d);  // repaint with/without underline
    return g_sem_debug;
}

std::string lsp_sync_semantic_report(Buffer& b) {
    if(b.nofile)
        return "run :semstat from the buffer you want to inspect, "
               "this one is a scratch buffer\n";
    std::string o = std::format("semantic tokens: {}\n\n", b.filename);
    o += std::format("option semantic_tokens : {}\n",
                     opts.semantic_tokens ? "on" : "off");
    o +=
        std::format("debug underline       : {}\n", g_sem_debug ? "on" : "off");
    Doc* d = find(b);
    if(!d) {
        o += is_waiting(b)
                 ? "server                 : starting, buffer is queued\n"
                 : "server                 : none attached (no lsp for this "
                   "filetype, or lsp disabled)\n";
        return o;
    }
    const Server* s = by_id(d->server);
    o += std::format("server                 : {} (id {}){}\n",
                     s ? s->name : "?",
                     d->server,
                     lsp_ready(d->server) ? "" : "  NOT READY");
    const Legend* lg = legend_for(d->server);
    o += std::format(
        "provider              : {}\n",
        lg ? std::format("full ok, {} token types", lg->names.size())
           : "NOT ADVERTISED (no semanticTokensProvider.full in the server "
             "capabilities)");
    const SemStats& st = d->sem;
    o += std::format("doc version           : {} (unsent edits: {})\n",
                     d->version,
                     b.pt.lsp_dirty() ? "yes" : "no");
    o += std::format(
        "requests              : sent {}  ok {}  "
        "stale-dropped {}  null {}  error {}\n",
        st.sent,
        st.ok,
        st.stale,
        st.empty,
        st.err);
    o += std::format("last request/reply    : v{} -> v{} in {:.0f} ms\n",
                     st.req_ver,
                     st.got_ver,
                     st.ms);
    if(!st.last_err.empty())
        o += std::format("last error            : {}\n", st.last_err);
    o += std::format(
        "tokens (v{})           : {} from server, {} painted, "
        "{} unmapped (left to tree-sitter)\n",
        st.got_ver,
        st.raw,
        st.applied,
        st.unmapped);

    if(&b == &E.buf() && !st.toks.empty()) {
        const size_t cur = cursor_offset();
        auto it = std::ranges::upper_bound(st.toks, cur, {}, &SemTok::start);
        if(it != st.toks.begin() && cur < std::prev(it)->end) {
            const SemTok& t = *std::prev(it);
            const auto& sty = lg ? lg->style[t.type] : std::nullopt;
            o += std::format(
                "cursor token          : '{}' type={} -> {}{}\n",
                b.pt.text(t.start, t.end - t.start),
                lg ? lg->names[t.type] : "?",
                sty ? std::format("painted #{:06x}", sty->fg & 0xFFFFFF)
                    : "unmapped (tree-sitter colour)",
                st.got_ver != d->version ? "  [tokens are stale]" : "");
        } else {
            o += "cursor token          : none under the cursor\n";
        }
    }

    if(lg) {
        o += "\nby type (tokens the server sent):\n";
        for(size_t i = 0; i < lg->names.size() && i < st.counts.size(); ++i)
            if(st.counts[i])
                o += std::format("  {:<16}{:>6}  {}\n",
                                 lg->names[i],
                                 st.counts[i],
                                 lg->style[i] ? "painted" : "tree-sitter");
    }
    return o;
}

void lsp_sync_semantic_refresh() {
    for(Doc& d : g_docs) request_tokens(d);
}
