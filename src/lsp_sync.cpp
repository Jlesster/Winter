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
#include "events.hpp"
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

// Every publishDiagnostics, kept per (server, file) so the workspace picker
// can list files that are not open. An empty publish erases the entry.
std::map<std::pair<LspId, std::string>, std::vector<PublishedDiag>> g_published;

struct SemTok {
    size_t start, end, type;
    uint32_t mods = 0;
};

struct SemStats {
    int sent = 0, ok = 0, stale = 0, empty = 0, err = 0;
    int req_ver = 0, got_ver = 0;
    double ms = 0;
    size_t raw = 0, applied = 0, unmapped = 0;
    std::string last_err;
    std::vector<size_t> counts;  // tokens per legend type id
    std::vector<SemTok> toks;    // every token as of got_ver, sorted by start
    std::string result_id;
    std::vector<int64_t> data;
    int deltas = 0, resyncs = 0;
    int retries = 0;  // back-off steps since the last good response
    int rid = 0;      // the request in flight (0: none)
    int cancelled = 0, refreshes = 0;
    TimerId retry_timer = 0;
};

struct Doc {
    Buffer* buf;
    LspId server;
    std::string uri, path;
    int version = 1;
    bool utf16 = true;
    int sync = 0;
    bool full_only = false;
    bool masked = false;  // the server holds a masked copy (lsp_sync_mask)
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
    std::string why;
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
    if(type == "parameter") {
        if(const auto v = theme_override("lsp.variable")) {
            Style st = syntax_theme("variable.parameter").value_or(Style{});
            style_patch(st, *v);
            return st;
        }
    }
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
        {"typeAlias", "type"},
        {"union", "type"},
        {"trait", "type"},
        {"generic", "type"},
        {"builtinType", "type.builtin"},
        {"selfKeyword", "variable.builtin"},
        {"selfTypeKeyword", "type.builtin"},
        {"lifetime", "label"},
        {"derive", "attribute"},
        {"deriveHelper", "attribute"},
        {"builtinAttribute", "attribute"},
        {"attribute", "attribute"},
        {"escapeSequence", "string.escape"},
        {"formatSpecifier", "string.escape"},
        {"boolean", "boolean"},

    };
    for(const auto& [name, cap] : k)
        if(name == type) return syntax_theme(cap);
    return std::nullopt;
}

struct Legend {
    bool ok = false, built = false, delta = false;
    uint64_t rev = 0;
    std::vector<std::string> names;
    std::vector<std::optional<Style>> style;
    std::vector<std::string> mods;
    mutable std::unordered_map<uint64_t, std::optional<Style>> combo;
};

std::map<LspId, Legend> g_legends;

const Legend* legend_for(LspId id) {
    Legend& lg = g_legends[id];
    if(!lg.built || lg.rev != theme_rev()) {
        lg = Legend{};
        lg.built = true;
        lg.rev = theme_rev();
        const Json& p = lsp_capabilities(id)["semanticTokensProvider"];
        const Json& full = p["full"];
        lg.ok = p.is_object() &&
                (full.is_bool() ? full.as_bool() : full.is_object());
        lg.delta = lg.ok && full.is_object() && full["delta"].as_bool();
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

// What a modifier does with no theme override: the syntax group to patch in
// (nullopt: nothing). `defaultLibrary` picks the .builtin capture of the
// token's own kind; `readonly` makes a variable/property read as a constant.
std::optional<Style> mod_default(std::string_view type, std::string_view mod) {
    if(mod == "defaultLibrary") {
        if(type == "function" || type == "method")
            return syntax_theme("function.builtin");
        if(type == "variable" || type == "property")
            return syntax_theme("variable.builtin");
        if(type == "type" || type == "class" || type == "struct" ||
           type == "enum" || type == "interface" || type == "typeAlias" ||
           type == "union" || type == "trait" || type == "concept")
            return syntax_theme("type.builtin");
        return std::nullopt;
    }
    if(mod == "readonly" && (type == "variable" || type == "property"))
        return syntax_theme("constant");
    return std::nullopt;
}

std::optional<Style> token_style_mods(const Legend& lg, size_t ty,
                                      uint64_t bits) {
    const uint64_t key = (uint64_t(ty) << 32) | (bits & 0xFFFFFFFF);
    if(const auto it = lg.combo.find(key); it != lg.combo.end())
        return it->second;
    std::optional<Style> out = lg.style[ty];
    for(size_t i = 0; i < lg.mods.size() && i < 32; ++i) {
        if(!((bits >> i) & 1)) continue;
        if(lg.mods[i] == "deprecated") {
            if(!out) out = Style{};
            out->attrs = static_cast<uint16_t>(out->attrs | ATTR_STRIKE);
        }
        if(const auto m = mod_default(lg.names[ty], lg.mods[i])) {
            if(!out) out = Style{};
            style_patch(*out, *m);
        }
        for(const std::string& g :
            {"lsp." + lg.names[ty] + "." + lg.mods[i], "lsp.mod." + lg.mods[i]})
            if(const auto m = theme_override(g)) {
                if(!out) out = Style{};
                style_patch(*out, *m);
            }
    }
    return lg.combo.emplace(key, out).first->second;
}

std::vector<int64_t> to_ints(const Json& data) {
    std::vector<int64_t> v;
    if(data.is_array())
        for(const Json& n : data.items()) v.push_back(n.as_int());
    return v;
}

// textDocument/semanticTokens/full/delta: splice `edits` into `old`. Edit
// offsets index the previous integer array; they must not overlap.
bool apply_delta(const std::vector<int64_t>& old, const Json& edits,
                 std::vector<int64_t>& out) {
    struct E {
        size_t start, del;
        const Json* data;
    };
    std::vector<E> es;
    for(const Json& e : edits.items()) {
        const int64_t s = e["start"].as_int(-1),
                      dc = e["deleteCount"].as_int(0);
        if(s < 0 || dc < 0) return false;
        es.push_back(
            {static_cast<size_t>(s), static_cast<size_t>(dc), &e["data"]});
    }
    std::ranges::stable_sort(es, {}, &E::start);
    out.clear();
    out.reserve(old.size());
    size_t at = 0;
    for(const E& e : es) {
        if(e.start < at || e.start + e.del > old.size()) return false;
        out.insert(out.end(), old.begin() + at, old.begin() + e.start);
        if(e.data->is_array())
            for(const Json& n : e.data->items()) out.push_back(n.as_int());
        at = e.start + e.del;
    }
    out.insert(out.end(), old.begin() + at, old.end());
    return true;
}

void apply_tokens(Doc& d, const Legend& lg, const std::vector<int64_t>& v) {
    Buffer& b = *d.buf;
    SemStats& st = d.sem;
    b.sync_anchors();  // flush pending deco shifts BEFORE adding new spans
    const NsId ns = deco_ns("lsp.semantic");
    st.raw = st.applied = st.unmapped = 0;
    st.counts.assign(lg.style.size(), 0);
    st.toks.clear();
    st.toks.reserve(v.size() / 5);
    std::vector<SpanDeco> spans;  // swapped in for the old ones in one pass
    spans.reserve(v.size() / 5);

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
        st.toks.push_back(
            {s, e, ty, static_cast<uint32_t>(v[k + 4] & 0xFFFFFFFF)});
        const auto base =
            token_style_mods(lg, ty, static_cast<uint64_t>(v[k + 4]));
        if(!base) {
            ++st.unmapped;
            continue;
        }
        Style sty = *base;
        if(g_sem_debug)
            sty.attrs = static_cast<uint16_t>(sty.attrs | ATTR_UNDERLINE);
        spans.push_back({s, e, ns, kPrioSemantic, sty, false, /*strict=*/true});
        ++st.applied;
    }
    b.decos.replace_ns_spans(ns, std::move(spans));
}

void request_tokens(Doc& d);

// The server had nothing for us yet (null result while it indexes) or gave up
// (ContentModified / ServerCancelled). The text didn't change, so no edit will
// ever re-ask: do it ourselves, backing off 200ms .. 6.4s, six tries at most.
void retry_tokens(Buffer* b) {
    Doc* d = find(*b);
    if(!d || d->sem.retries >= 6) return;
    if(d->sem.retry_timer) loop_cancel(d->sem.retry_timer);
    const int ms = 200 << d->sem.retries++;
    d->sem.retry_timer = loop_after(ms, [b] {
        if(Doc* d = find(*b)) {
            d->sem.retry_timer = 0;
            request_tokens(*d);
        }
    });
}

// Stop waiting for the request in flight (if any) and any scheduled retry.
void cancel_tokens(Doc& d) {
    SemStats& st = d.sem;
    if(st.retry_timer) {
        loop_cancel(st.retry_timer);
        st.retry_timer = 0;
    }
    if(st.rid) {
        lsp_cancel(d.server, st.rid);
        st.rid = 0;
        ++st.cancelled;
    }
}

void request_tokens(Doc& d) {
    Buffer* b = d.buf;
    if(!opts.semantic_tokens) {
        cancel_tokens(d);
        b->decos.clear_ns(deco_ns("lsp.semantic"));
        return;
    }
    const Legend* leg = legend_for(d.server);
    if(!leg) return;
    if(d.masked) return;
    // One request in flight per document: a new one supersedes the old. That
    // keeps two deltas from ever being asked against the same previousResultId
    // (the second reply would then not match our chain and force a resync) and
    // spares the server work whose answer we would only throw away as stale.
    cancel_tokens(d);
    const int ver = d.version;
    d.sem.req_ver = ver;
    ++d.sem.sent;
    const bool use_delta = leg->delta && !d.sem.result_id.empty();
    const std::string base_id = use_delta ? d.sem.result_id : std::string{};
    Json params = Json::obj({{"textDocument", Json::obj({{"uri", d.uri}})}});
    if(use_delta) params.set("previousResultId", base_id);
    const auto t0 = std::chrono::steady_clock::now();
    const int rid = lsp_request(
        d.server,
        use_delta ? "textDocument/semanticTokens/full/delta"
                  : "textDocument/semanticTokens/full",
        std::move(params),
        [b, ver, t0, use_delta, base_id](const Json& result,
                                         const Json& error) {
            Doc* d = find(*b);  // the buffer may have been closed
            if(!d) return;
            SemStats& st = d->sem;
            st.rid = 0;  // answered (a cancelled request never gets here)
            // stale: text moved on since we asked; the next flush re-asks.
            // The chain below still advances so the next delta has a base.
            const bool stale =
                d->masked || d->version != ver || b->pt.lsp_dirty();
            const auto drop_chain = [&] {
                st.result_id.clear();
                st.data.clear();
            };
            if(!error.is_null()) {
                drop_chain();
                if(stale) {
                    ++st.stale;
                    return;
                }
                ++st.err;
                st.last_err = std::string(error["message"].as_str("?"));
                if(const auto c = error["code"].as_int(0);
                   c == -32801 || c == -32802)
                    retry_tokens(b);
                return;
            }
            if(!result.is_object()) {
                drop_chain();
                ++(stale ? st.stale : st.empty);
                if(!stale) retry_tokens(b);
                return;
            }
            std::vector<int64_t> nd;
            bool ok = false;
            if(result["edits"].is_array()) {
                ok = use_delta && st.result_id == base_id &&
                     apply_delta(st.data, result["edits"], nd);
                if(ok) ++st.deltas;
            } else if(result["data"].is_array()) {
                nd = to_ints(result["data"]);
                ok = true;
            }
            if(!ok) {  // delta against a base we no longer hold: resync
                drop_chain();
                ++st.resyncs;
                if(stale) ++st.stale;
                else request_tokens(*d);  // full this time (chain is empty)
                return;
            }
            st.data = std::move(nd);
            st.result_id = std::string(result["resultId"].as_str());
            if(stale) {
                ++st.stale;
                return;
            }
            st.ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
            st.got_ver = ver;
            st.retries = 0;
            ++st.ok;
            if(const Legend* lg = legend_for(d->server))
                apply_tokens(*d, *lg, st.data);
        });
    // 0 when the server was not running: the callback already ran
    if(Doc* cur = find(*b); cur && rid) cur->sem.rid = rid;
}

// workspace/semanticTokens/refresh: the server's idea of the tokens changed
// (it finished indexing, its config moved). Whatever we hold is void, deltas
// included. Answered at once; the re-ask is one loop turn later so a handler
// that touches g_docs is not run from inside the reader.
Json on_sem_refresh(LspId id, const Json&) {
    loop_after(0, [id] {
        for(Doc& d : g_docs) {
            if(d.server != id) continue;
            d.sem.result_id.clear();
            d.sem.data.clear();
            d.sem.retries = 0;
            ++d.sem.refreshes;
            request_tokens(d);
        }
    });
    return Json();
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
    if(d.masked) {
        // The server has a masked copy: put the real text back in full. Any
        // pending edit is already part of it.
        d.masked = false;
        d.buf->pt.take_lsp_edit();
        lsp_notify(
            d.server,
            "textDocument/didChange",
            Json::obj(
                {{"textDocument",
                  Json::obj({{"uri", d.uri}, {"version", ++d.version}})},
                 {"contentChanges",
                  Json::arr({Json::obj({{"text", full_text(*d.buf)}})})}}));
        request_tokens(d);
        return;
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

// lsp_attach / lsp_detach {server}, one loop turn later: a handler may open
// buffers or start servers, which would invalidate what the caller is
// iterating.
void announce_lsp(const char* event, uint64_t buf_id, std::string server) {
    loop_after(0, [event, buf_id, server = std::move(server)] {
        for(const auto& b : E.buffers)
            if(b->id == buf_id) {
                EventBufScope scope(b.get());
                events_emit(event, {server});
                return;
            }
    });
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
    announce_lsp("lsp_attach", b.id, s.name);
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
    s->why = why.substr(0, why.find('\n'));
    s->waiting.clear();
    std::erase_if(g_published,
                  [id](const auto& kv) { return kv.first.first == id; });
    const std::string server_name = s->name;
    g_legends.erase(id);  // ids are never reused; don't let the cache grow
    std::vector<uint64_t> detached;  // announced below, after g_docs is stable
    for(Doc& d : g_docs)
        if(d.server == id) {
            detached.push_back(d.buf->id);
            if(d.timer) loop_cancel(d.timer);
            if(d.sem.retry_timer) loop_cancel(d.sem.retry_timer);
            d.buf->diags.clear();
            d.buf->decos.clear_ns(deco_ns("lsp.semantic"));
            d.buf->decos.clear_ns(deco_ns("lsp.refs"));
            d.buf->pt.lsp_track(false);
        }
    std::erase_if(g_docs, [&](const Doc& d) { return d.server == id; });
    for(const uint64_t buf : detached)
        announce_lsp("lsp_detach", buf, server_name);
    if(s->restart) {
        editor_set_status_message("lsp {}: restarting", s->name);
        std::erase_if(g_servers, [id](const Server& x) { return x.id == id; });
        loop_after(0, [] {
            for(auto& b : E.buffers) lsp_sync_attach(*b);
        });
        return;
    }
    editor_set_status_message(
        "lsp {} exited: {}", s->name, why.substr(0, why.find('\n')));
}

void store_published(LspId id, const std::string& path, const Json& list) {
    std::vector<PublishedDiag> v;
    if(list.is_array())
        for(const Json& j : list.items()) {
            const Json& s = j["range"]["start"];
            v.push_back({path,
                         static_cast<int>(s["line"].as_int()),
                         static_cast<int>(s["character"].as_int()),
                         lsp_encoding(id) != "utf-8",
                         static_cast<int>(
                             std::clamp<int64_t>(j["severity"].as_int(1), 1, 4)),
                         std::string(j["message"].as_str()),
                         std::string(j["source"].as_str())});
        }
    const auto key = std::make_pair(id, path);
    if(v.empty()) g_published.erase(key);
    else g_published[key] = std::move(v);
}

void on_diagnostics(LspId id, const Json& p) {
    const std::string path = lsp_path(p["uri"].as_str());
    store_published(id, path, p["diagnostics"]);
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
        lsp_on_request("workspace/semanticTokens/refresh", on_sem_refresh);
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

std::string lsp_sync_server_report() {
    if(g_servers.empty()) return "no lsp server has been started\n";
    std::string o;
    for(const Server& s : g_servers) {
        const auto docs = std::ranges::count_if(
            g_docs, [&](const Doc& d) { return d.server == s.id; });
        o += std::format("{} (id {}): {}{}\n",
                         s.name,
                         s.id,
                         s.dead            ? "dead: " + s.why
                         : lsp_ready(s.id) ? std::string("ready")
                                           : std::string("starting"),
                         s.restart ? "  [restart pending]" : "");
        o += std::format(
            "  attached buffers: {} queued: {}\n", docs, s.waiting.size());
        if(!s.dead && !lsp_stderr(s.id).empty())
            o += std::format("  stderr tail:\n{}\n", lsp_stderr(s.id));
    }
    return o;
}

void lsp_sync_tick() {
    ref_tick();
    for(Doc& d : g_docs)
        if(!d.timer && (d.masked || d.buf->pt.lsp_dirty()))
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
    const Server* owner = by_id(d->server);
    const std::string server = owner ? owner->name : std::string();
    if(d->timer) loop_cancel(d->timer);
    cancel_tokens(*d);
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
    EventBufScope scope(&b);  // last thing we do: a handler may touch anything
    events_emit("lsp_detach", {server});
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

std::optional<LspDoc> lsp_sync_mask(
    Buffer& b, const std::vector<std::pair<size_t, size_t>>& ranges) {
    Doc* d = find(b);
    if(!d || !lsp_ready(d->server)) return std::nullopt;
    flush(*d);  // real edits first; this also lifts an earlier mask
    if(!ranges.empty() && d->sync) {
        std::string text = full_text(b);
        size_t prev_end = 0;
        bool ok = true;
        for(const auto& [a, z] : ranges)
            if(a < prev_end || z <= a || z > b.pt.size()) ok = false;
            else prev_end = z;
        if(ok) {
            for(auto it = ranges.rbegin(); it != ranges.rend(); ++it)
                text.erase(it->first, it->second - it->first);
            lsp_notify(
                d->server,
                "textDocument/didChange",
                Json::obj(
                    {{"textDocument",
                      Json::obj({{"uri", d->uri}, {"version", ++d->version}})},
                     {"contentChanges",
                      Json::arr({Json::obj({{"text", text}})})}}));
            d->masked = true;
        }
    }
    const Server* sv = by_id(d->server);
    return LspDoc{d->server, d->uri, d->utf16, sv ? sv->name : std::string{}};
}

bool lsp_sync_masked(Buffer& b) {
    const Doc* d = find(b);
    return d && d->masked;
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
    o += std::format("superseded/refresh    : {} cancelled in flight, "
                     "{} server refreshes\n",
                     st.cancelled,
                     st.refreshes);
    o += std::format("delta                 : {} ({} applied, {} resyncs)\n",
                     lg && lg->delta ? "supported" : "not offered by server",
                     st.deltas,
                     st.resyncs);

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
            std::string mods;
            if(lg)
                for(size_t i = 0; i < lg->mods.size() && i < 32; ++i)
                    if((t.mods >> i) & 1)
                        mods += (mods.empty() ? "" : ",") + lg->mods[i];
            const auto fin =
                lg ? token_style_mods(*lg, t.type, t.mods) : std::nullopt;
            o += std::format(
                "cursor token          : '{}' type={} mods=[{}] final={}\n"
                "                        base ->",
                b.pt.text(t.start, t.end - t.start),
                lg ? lg->names[t.type] : "?",
                mods,
                fin ? std::format("#{:06x}", fin->fg & 0xFFFFFF)
                    : "none (tree-sitter)");
            o += std::format(
                " {}{}\n",
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

std::vector<PublishedDiag> lsp_sync_published() {
    std::vector<PublishedDiag> out;
    for(const auto& kv : g_published)
        out.insert(out.end(), kv.second.begin(), kv.second.end());
    return out;
}

void lsp_sync_semantic_refresh() {
    for(Doc& d : g_docs) request_tokens(d);
}
