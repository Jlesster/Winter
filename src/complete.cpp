#include "complete.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "columns.hpp"
#include "buflist.hpp"
#include "command.hpp"
#include "core.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "lsp_client.hpp"
#include "lsp_sync.hpp"
#include "overlay.hpp"
#include "piece.hpp"
#include "snippet.hpp"
#include "theme.hpp"
#include "utf8.hpp"

namespace {
constexpr int kDelayMs = 40;
constexpr size_t kMinPrefix = 2;
constexpr size_t kMaxItems = 400;
constexpr int kMaxRows = 8;
constexpr int kMaxLabelW = 64, kMaxDetailW = 22;
constexpr int kResolveMs = 60;
constexpr size_t kDocRows = 6;
constexpr int kDocW = 46;

constexpr std::string_view kKinds = "?tmfcFvCIMpuVEksofrdeKSvOT";

struct Edit {
    size_t start, end;
    std::string text;
};

struct Item {
    std::string label, filter, sort, detail;
    int kind = 0;
    std::string text;
    bool snippet = false;
    size_t start = 0;
    size_t tail = 0;
    std::vector<Edit> extra;
    int rank = 0;
    std::string doc;
    Json raw;
    bool resolved = false;
    bool resolving = false;
};

struct Session {
    Buffer* buf = nullptr;
    size_t word_start = 0;
    size_t line = 0;
    bool incomplete = false;
    std::vector<Item> items;
    std::vector<size_t> shown;
    size_t sel = 0, top = 0;
    OverlayId id = 0;
    unsigned gen = 0;
    LspId server = 0;
    bool utf16 = true;
    bool can_resolve = false;
    unsigned epoch = 0;
    std::vector<std::string> doc_lines;
};

Session S;
unsigned g_seq = 0;
TimerId g_timer = 0;
TimerId g_rtimer = 0;
std::string g_last;

bool word_byte(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

size_t word_start_at(const PieceTable& pt, size_t cursor) {
    const size_t ls = pt.line_start(pt.line_of(cursor));
    size_t s = cursor;
    while(s > ls && word_byte(static_cast<unsigned char>(pt.at(s - 1)))) --s;
    return s;
}

std::string fit(std::string_view s, int w) {
    std::string out;
    int used = 0;
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        const int cw = cell_width(d.cp, 0);
        if(used + cw > w) break;
        out.append(s.substr(i, d.len));
        used += cw;
        i += d.len;
    }
    return out;
}

std::string doc_text(const Json& d) {
    return std::string(d.is_object() ? d["value"].as_str() : d.as_str());
}

std::vector<std::string> wrap(std::string_view text, int w, size_t max) {
    std::vector<std::string> out;
    size_t pos = 0;
    while(pos <= text.size() && out.size() < max) {
        const size_t nl = text.find('\n', pos);
        std::string_view para =
            text.substr(pos, nl == std::string_view::npos ? nl : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        while(!para.empty() &&
              std::isspace(static_cast<unsigned char>(para.front())))
            para.remove_prefix(1);
        if(para.starts_with("```")) continue;
        if(para.empty()) {
            if(!out.empty() && !out.back().empty()) out.emplace_back();
            continue;
        }
        std::string line;
        for(size_t i = 0; i < para.size() && out.size() < max;) {
            size_t j = para.find(' ', i);
            if(j == std::string_view::npos) j = para.size();
            const std::string word = fit(para.substr(i, j - i), w);
            i = j + 1;
            if(word.empty()) continue;
            if(!line.empty() && text_width(line) + 1 + text_width(word) > w) {
                out.push_back(std::move(line));
                line.clear();
                if(out.size() >= max) break;
            }
            if(!line.empty()) line += ' ';
            line += word;
        }
        if(!line.empty() && out.size() < max) out.push_back(std::move(line));
    }
    while(!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

std::vector<Edit> parse_extras(const Json& list, const PieceTable& pt,
                               bool utf16, size_t limit) {
    std::vector<Edit> out;
    if(!list.is_array()) return out;
    for(const Json& e : list.items()) {
        const Json& r = e["range"];
        Edit ed{lsp_offset(pt,
                           r["start"]["line"].as_int(),
                           r["start"]["character"].as_int(),
                           utf16),
                lsp_offset(pt,
                           r["end"]["line"].as_int(),
                           r["end"]["character"].as_int(),
                           utf16),
                std::string(e["newText"].as_str())};
        if(ed.end > limit) return {};
        out.push_back(std::move(ed));
    }
    return out;
}

std::ptrdiff_t apply_extras(PieceTable& pt, std::vector<Edit> extra) {
    std::ranges::sort(extra, std::greater{}, &Edit::start);
    std::ptrdiff_t shift = 0;
    for(const Edit& e : extra) {
        if(e.end > e.start) pt.remove(e.start, e.end - e.start);
        pt.insert(e.start, e.text);
        shift += static_cast<std::ptrdiff_t>(e.text.size()) -
                 static_cast<std::ptrdiff_t>(e.end - e.start);
    }
    return shift;
}

Item parse_item(const Json& j, const PieceTable& pt, bool utf16,
                size_t word_start, size_t cursor) {
    Item it;
    it.raw = j;
    it.label = std::string(j["label"].as_str());
    it.label.erase(0, it.label.find_first_not_of(' '));
    it.filter = std::string(j["filterText"].as_str(it.label));
    it.sort = std::string(j["sortText"].as_str(it.label));
    it.detail = std::string(j["detail"].as_str());
    if(it.detail.empty())
        it.detail = std::string(j["labelDetails"]["description"].as_str());
    it.doc = doc_text(j["documentation"]);
    it.kind = static_cast<int>(j["kind"].as_int(0));
    it.snippet = j["insertTextFormat"].as_int(1) == 2;
    const Json& te = j["textEdit"];
    it.text =
        std::string(te["newText"].as_str(j["insertText"].as_str(it.label)));
        it.start = word_start;
        // plain TextEdit has "range"; InsertReplaceEdit has "insert"+"replace".
        // We take "replace" so no tail of the word is left behind; use
        // te["insert"] here instead for VS Code's default insert behaviour.
        const Json& rg = te["range"].is_object() ? te["range"] : te["replace"];
        if(rg.is_object()) {
            it.start = lsp_offset(pt,
                                  rg["start"]["line"].as_int(),
                                  rg["start"]["character"].as_int(),
                                  utf16);
            const size_t end = lsp_offset(pt,
                                          rg["end"]["line"].as_int(),
                                          rg["end"]["character"].as_int(),
                                          utf16);
            it.tail = end > cursor ? end - cursor : 0;
        }
        it.start = std::min(it.start, cursor);
        it.extra = parse_extras(j["additionalTextEdits"], pt, utf16, it.start);
        return it;
}

bool has_upper(std::string_view s) {
    return std::ranges::any_of(
        s, [](unsigned char c) { return std::isupper(c) != 0; });
}

bool screaming(std::string_view s) {
    return s.size() >= 2 && has_upper(s) &&
           !std::ranges::any_of(
               s, [](unsigned char c) { return std::islower(c) != 0; });
}

bool word_start(std::string_view f, size_t i) {
    if(i == 0) return true;
    const unsigned char p = static_cast<unsigned char>(f[i - 1]);
    const unsigned char c = static_cast<unsigned char>(f[i]);
    return !std::isalnum(p) || (std::islower(p) && std::isupper(c));
}

int rank(std::string_view f, std::string_view q) {
    if(q.empty()) return 0;
    const auto ieq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };
    if(f.size() >= q.size() && std::equal(q.begin(), q.end(), f.begin(), ieq)) {
        if(has_upper(q) && !f.starts_with(q)) return 1;
        if(!has_upper(q) && screaming(f)) return 1;
        return 0;
    }
    for(size_t i = 0; i < f.size(); ++i) {
        if(!ieq(f[i], q[0]) || !word_start(f, i)) continue;
        size_t qi = 1;
        for(size_t j = i + 1; j < f.size() && qi < q.size(); ++j)
            if(ieq(f[j], q[qi])) ++qi;
        if(qi == q.size()) return 2;
    }
    return -1;
}

bool refilter() {
    const PieceTable& pt = S.buf->pt;
    const size_t cur = cursor_offset();
    const std::string q = pt.text(S.word_start, cur - S.word_start);
    S.shown.clear();
    for(size_t i = 0; i < S.items.size(); ++i) {
        Item& it = S.items[i];
        const bool own = it.start != S.word_start && it.start <= cur;
        it.rank = rank(it.filter, own ? pt.text(it.start, cur - it.start) : q);
        if(it.rank >= 0) S.shown.push_back(i);
    }
    std::ranges::stable_sort(S.shown, [](size_t a, size_t b) {
        const Item &x = S.items[a], &y = S.items[b];
        return x.rank != y.rank ? x.rank < y.rank : x.sort < y.sort;
    });
    S.sel = S.top = 0;
    return !S.shown.empty();
}

void reset() {
    if(g_rtimer) loop_cancel(g_rtimer);
    g_rtimer = 0;
    ++S.epoch;
    S.doc_lines.clear();
    S.can_resolve = false;
    S.items.clear();
    S.shown.clear();
    S.sel = S.top = 0;
    S.id = 0;
    S.buf = nullptr;
    S.incomplete = false;
}

void cancel_pending();

void close() {
    const OverlayId id = S.id;
    ++S.gen;
    ++g_seq;
    cancel_pending();
    reset();
    if(id) overlay_close(id);
}

void resolve_late(Buffer* buf, LspId server, bool utf16, const Item& it,
                  size_t start) {
    const auto doc = lsp_sync_doc(*buf, true);
    if(!doc) return;
    const uint64_t rev = buf->pt.revision();
    lsp_request(
        server,
        "completionItem/resolve",
        it.raw,
        [=](const Json& result, const Json& error) {
            if(!error.is_null() || !result.is_object()) return;
            const Json& e = result["additionalTextEdits"];
            if(!e.is_array() || e.items().empty()) return;
            if(buf != &E.buf() || buf->pt.revision() != rev ||
               cursor_offset() < start) {
                editor_set_status_message(
                    "completion: auto-import skipped (buffer changed)");
                return;
            }
            PieceTable& pt = buf->pt;
            auto extra = parse_extras(e, pt, utf16, start);
            if(extra.empty()) return;
            const size_t cur = cursor_offset();
            PieceTable::Group g(pt);
            const std::ptrdiff_t shift = apply_extras(pt, std::move(extra));
            set_cursor_offset(
                static_cast<size_t>(static_cast<std::ptrdiff_t>(cur) + shift));
        });
}

bool has_unmatched_tail(std::string_view t) {
    if(t.empty()) return false;
    const char c = t.back();
    char open = 0;
    switch(c) {
        case '>': open = '<'; break;
        case '}': open = '{'; break;
        case ']': open = '['; break;
        case ')': open = '('; break;
        case '"':
        case '\'': break;
        default: return false;
    }
    int n = 0;
    for(size_t i = 0; i < t.size(); ++i) {
        if(t[i] == '\\') {
            ++i;
            continue;
        }
        if(open) {
            if(t[i] == open) ++n;
            else if(t[i] == c) --n;
        } else if(t[i] == c) ++n;
    }
    return open ? n < 0 : n % 2 != 0;
}

void accept() {
    if(S.shown.empty() || !S.buf) return close();
    Item it = S.items[S.shown[S.sel]];
    Buffer* buf = S.buf;
    const LspId server = S.server;
    const bool utf16 = S.utf16;
    const bool late = S.can_resolve && !it.resolved && it.extra.empty();
    PieceTable& pt = buf->pt;
    const auto cur = static_cast<std::ptrdiff_t>(cursor_offset());
    close();

    size_t start;
    {
        PieceTable::Group g(pt);
        const std::ptrdiff_t shift = apply_extras(pt, std::move(it.extra));
        start = static_cast<size_t>(static_cast<std::ptrdiff_t>(it.start) + shift);
        std::ptrdiff_t end = std::clamp<std::ptrdiff_t>(
            cur + shift + static_cast<std::ptrdiff_t>(it.tail),
            static_cast<std::ptrdiff_t>(start),
            static_cast<std::ptrdiff_t>(pt.size()));
        if(!it.text.empty() && end < static_cast<std::ptrdiff_t>(pt.size()) &&
           pt.at(static_cast<size_t>(end)) == it.text.back() &&
               has_unmatched_tail(it.text))
            ++end;
        const std::string gone =
            end > static_cast<std::ptrdiff_t>(start)
            ? pt.text(start, static_cast<size_t>(end) - start)
            : std::string();
            g_last = std::format(
                "label   : {}\nstart   : {}  tail: {}  cursor: {}  extras shift: {}\n"
                "replaced: [{}]\ninserted: [{}]  snippet: {}\nraw item: {}\n",
                it.label, start, it.tail, cur, shift, gone, it.text, it.snippet,
                it.raw.dump());
            if(end > static_cast<std::ptrdiff_t>(start))
                pt.remove(start, static_cast<size_t>(end) - start);
            if(it.snippet) {
                set_cursor_offset(start);
                snippet_expand(it.text);
            } else {
                pt.insert(start, it.text);
                set_cursor_offset(start + it.text.size());
            }
    }
    if(late) resolve_late(buf, server, utf16, it, start);
}

void show();
bool popup_key(int c) {
    if(!S.id || S.shown.empty()) return false;
    const auto step = [](long d) {
        const long n = static_cast<long>(S.shown.size());
        S.sel = static_cast<size_t>((static_cast<long>(S.sel) + d + n) % n);
    };
    switch(c) {
        case CTRL_KEY('n'):
        case ARROW_DOWN:
            step(+1);
            show();
            return true;
        case CTRL_KEY('p'):
        case ARROW_UP:
            step(-1);
            show();
            return true;
        case '\t':
        case CTRL_KEY('y'): accept(); return true;
        case CTRL_KEY('e'): close(); return true;
        default: return false;
    }
}

void paint(OverlayId id, int lw, int dw, int inner, int rows) {
    overlay_clear(id);
    if(S.sel < S.top) S.top = S.sel;
    else if(S.sel >= S.top + static_cast<size_t>(rows))
        S.top = S.sel - rows + 1;
    for(int r = 0; r < rows; ++r) {
        const size_t idx = S.top + static_cast<size_t>(r);
        if(idx >= S.shown.size()) break;
        const Item& it = S.items[S.shown[idx]];
        Style row = theme_ui("ui.menu", Style{});
        if(idx == S.sel)
            row = theme_ui("ui.menu.select", Style{.attrs = ATTR_REVERSE});
        Style kind = row, dim = row;
        style_patch(kind, theme_ui("ui.menu.kind", Style{.attrs = ATTR_BOLD}));
        style_patch(dim, theme_ui("ui.menu.detail", Style{.attrs = ATTR_DIM}));
        const int y = 1 + r;
        const char k =
            kKinds[it.kind > 0 && it.kind < static_cast<int>(kKinds.size())
                       ? it.kind
                       : 0];
        overlay_put(
            id, 1, y, std::string(static_cast<size_t>(inner), ' '), row);
        overlay_put(id, 2, y, std::string(1, k), kind);
        overlay_put(id, 4, y, fit(it.label, lw), row);
        if(dw) overlay_put(id, 4 + lw + 2, y, fit(it.detail, dw), dim);
    }
    if(S.doc_lines.empty()) return;
    const Style dim = theme_ui("ui.menu.doc", Style{.attrs = ATTR_DIM});
    std::string rule;
    for(int i = 0; i < inner; i++) rule += "\u2500";
    overlay_put(id, 1, 1 + rows, rule, dim);
    for(size_t i = 0; i < S.doc_lines.size(); ++i)
        overlay_put(id,
                    2,
                    2 + rows + static_cast<int>(i),
                    fit(S.doc_lines[i], inner - 2),
                    dim);
}

void resolve_selected() {
    g_rtimer = 0;
    if(!S.id || !S.can_resolve || S.shown.empty()) return;
    const size_t idx = S.shown[S.sel];
    Item& cur = S.items[idx];
    if(cur.resolved || cur.resolving) return;
    Buffer* buf = S.buf;
    if(!lsp_sync_doc(*buf, true)) return;
    cur.resolving = true;
    const unsigned epoch = S.epoch;
    const uint64_t rev = buf->pt.revision();
    lsp_request(
        S.server,
        "completionItem/resolve",
        cur.raw,
        [=](const Json& result, const Json& error) {
            if(epoch != S.epoch || idx >= S.items.size()) return;
            Item& it = S.items[idx];
            it.resolving = false;
            if(!error.is_null() || !result.is_object()) {
                it.resolved = true;
                return;
            }
            if(const std::string_view d = result["detail"].as_str(); !d.empty())
                it.detail = std::string(d);
            if(const std::string doc = doc_text(result["documentation"]);
               !doc.empty())
                it.doc = doc;
            if(buf->pt.revision() == rev) {
                if(it.extra.empty())
                    it.extra = parse_extras(result["additionalTextEdits"],
                                            buf->pt,
                                            S.utf16,
                                            it.start);
                it.resolved = true;
            }
            show();
        });
}

void show() {
    if(S.shown.empty()) return close();
    const unsigned gen = ++S.gen;
    if(S.id) overlay_close(S.id);

    int lw = 0, dw = 0;
    for(const size_t i : S.shown) {
        lw = std::max(lw, text_width(S.items[i].label));
        dw = std::max(dw, text_width(S.items[i].detail));
    }
    dw = std::min(dw, kMaxDetailW);
    const int room = std::max(24, E.term_cols - 8 - (dw ? 2 + dw : 0));
    lw = std::min({lw, kMaxLabelW, room});
    int inner = 1 + 1 + 1 + lw + (dw ? 2 + dw : 0) + 1;
    const Item& sel = S.items[S.shown[S.sel]];
    if(!sel.doc.empty()) inner = std::max(inner, kDocW);
    S.doc_lines = sel.doc.empty() ? std::vector<std::string>{}
                                  : wrap(sel.doc, inner - 2, kDocRows);
    const int rows = std::min<int>(kMaxRows, static_cast<int>(S.shown.size()));
    const int doc_h =
        S.doc_lines.empty() ? 0 : 1 + static_cast<int>(S.doc_lines.size());
    const std::string prefix =
        S.buf->pt.text(S.word_start, cursor_offset() - S.word_start);

    OverlaySpec o;
    o.at = OverlaySpec::At::Cursor;
    o.x = -text_width(prefix);
    o.y = 1;
    o.w = inner + 2;
    o.h = rows + doc_h + 2;
    o.z = 50;
    o.framed = true;
    o.dismiss_on_move = false;
    o.on_key = popup_key;
    o.on_close = [gen] {
        if(S.gen == gen) reset();
    };
    o.on_paint = [lw, dw, inner, rows](OverlayId id) {
        paint(id, lw, dw, inner, rows);
    };
    S.id = overlay_open(std::move(o));

    if(S.can_resolve) {
        if(g_rtimer) loop_cancel(g_rtimer);
        g_rtimer = loop_after(kResolveMs, resolve_selected);
    }
}

bool supports(LspId s) {
    return lsp_ready(s) &&
           lsp_capabilities(s)["completionProvider"].is_object();
}

bool is_trigger(LspId s, int c) {
    const Json& t =
        lsp_capabilities(s)["completionProvider"]["triggerCharacters"];
    if(!t.is_array()) return false;
    for(const Json& x : t.items()) {
        const std::string_view v = x.as_str();
        if(v.size() == 1 && v[0] == static_cast<char>(c)) return true;
    }
    return false;
}

void on_reply(unsigned seq, uint64_t rev, Buffer* buf, LspId server,
              size_t word_start, size_t cur, bool utf16, const Json& result,
              const Json& error) {
    if(seq != g_seq || buf != &E.buf() || E.mode != EditorMode::INSERT) return;
    if(!error.is_null()) return;
    if(buf->pt.revision() != rev || cursor_offset() != cur) return;
    const Json& list = result.is_array() ? result : result["items"];
    if(!list.is_array()) return close();
    S.items.clear();
    ++S.epoch;
    S.incomplete = result.is_object() && result["isIncomplete"].as_bool(false);
    S.can_resolve =
        lsp_capabilities(server)["completionProvider"]["resolveProvider"]
            .as_bool(false);
    for(const Json& j : list.items()) {
        if(S.items.size() >= kMaxItems) break;
        S.items.push_back(parse_item(j, buf->pt, utf16, word_start, cur));
        S.items.back().resolved = !S.can_resolve;
    }
    S.server = server;
    S.utf16 = utf16;
    S.buf = buf;
    S.word_start = word_start;
    S.line = buf->pt.line_of(cursor_offset());
    if(refilter()) show();
    else close();
}

void request(int kind, const std::string& trigger) {
    if(E.mode != EditorMode::INSERT) return;
    Buffer& b = E.buf();
    const auto doc = lsp_sync_doc(b, true);
    if(!doc || !supports(doc->server)) return;
    const PieceTable& pt = b.pt;
    const size_t cur = cursor_offset();
    const size_t ws = word_start_at(pt, cur);
    Json ctx = Json::obj({{"triggerKind", kind}});
    if(!trigger.empty()) ctx.set("triggerCharacter", trigger);
    const unsigned seq = ++g_seq;
    const uint64_t rev = pt.revision();
    Buffer* buf = &b;
    const bool utf16 = doc->utf16;
    const LspId server = doc->server;
    lsp_request(
        doc->server,
        "textDocument/completion",
        Json::obj({{"textDocument", Json::obj({{"uri", doc->uri}})},
                   {"position",
                    Json::obj({{"line", pt.line_of(cur)},
                               {"character", lsp_col(pt, cur, utf16)}})},
                   {"context", ctx}}),
        [=](const Json& result, const Json& error) {
            on_reply(seq, rev, buf, server, ws, cur, utf16, result, error);
        });
}

void cancel_pending() {
    if(g_timer) loop_cancel(g_timer);
    g_timer = 0;
}

void schedule(int kind, std::string trigger = {}) {
    cancel_pending();
    g_timer = loop_after(kDelayMs, [kind, trigger = std::move(trigger)] {
        g_timer = 0;
        request(kind, trigger);
    });
}

}  // namespace

bool completion_active() { return S.id != 0; }

void hover_show() {
    static OverlayId g_hover = 0;
    Buffer& b = E.buf();
    const auto doc = lsp_sync_doc(b, true);
    if(!doc) {
        editor_set_status_message("hover: no language server");
        return;
    }
    const size_t cur = cursor_offset();
    lsp_request(
        doc->server,
        "textDocument/hover",
        Json::obj(
            {{"textDocument", Json::obj({{"uri", doc->uri}})},
             {"position",
              Json::obj({{"line", b.pt.line_of(cur)},
                         {"character", lsp_col(b.pt, cur, doc->utf16)}})}}),
        [buf = &b, rev = b.pt.revision()](const Json& result,
                                          const Json& error) {
            if(buf != &E.buf() || buf->pt.revision() != rev) return;
            if(!error.is_null() || !result.is_object()) {
                editor_set_status_message("hover: nothing here");
                return;
            }
            std::string text;
            const Json& c = result["contents"];
            if(c.is_array())
                for(const Json& e : c.items()) text += doc_text(e) + "\n\n";
            else text = doc_text(c);

            const int w = std::min(64, E.term_cols - 4);
            auto lines = wrap(text, w - 2, 14);
            if(lines.empty()) {
                editor_set_status_message("hover: nothing here");
                return;
            }
            if(g_hover) overlay_close(g_hover);
            OverlaySpec o;
            o.at = OverlaySpec::At::Cursor;
            o.x = 0;
            o.y = 1;
            o.w = w + 2;
            o.h = static_cast<int>(lines.size()) + 2;
            o.z = 50;
            o.framed = true;
            o.on_paint = [lines, w](OverlayId id) {
                overlay_clear(id);
                for(size_t i = 0; i < lines.size(); ++i)
                    overlay_put(id,
                                2,
                                1 + static_cast<int>(i),
                                fit(lines[i], w - 2),
                                theme_ui("ui.hover", Style{}));
            };
            g_hover = overlay_open(std::move(o));
        });
}

void completion_close() {
    cancel_pending();
    close();
}

void completion_post_key(int c) {
    if(E.mode != EditorMode::INSERT || (S.buf && S.buf != &E.buf())) {
        completion_close();
        return;
    }
    Buffer& b = E.buf();
    const auto doc = lsp_sync_doc(b);
    if(!doc || !supports(doc->server)) return;
    const PieceTable& pt = b.pt;
    const size_t cur = cursor_offset();
    const size_t ws = word_start_at(pt, cur);
    const bool ascii = c >= 32 && c < 127;
    const bool typed_word =
        (ascii && word_byte(static_cast<unsigned char>(c))) ||
        (c >= 0x80 && c < 0x100);
    if(S.id) {
        if(ws != S.word_start || pt.line_of(cur) != S.line) {
            close();
        } else {
            if(!refilter()) return close();
            show();
            if(S.incomplete & typed_word) schedule(3);
            return;
        }
    }
    if(c == CTRL_KEY('n')) return schedule(1);
    if(ascii && is_trigger(doc->server, c))
        return schedule(2, std::string(1, static_cast<char>(c)));
    if(typed_word && cur - ws >= kMinPrefix) return schedule(1);
    cancel_pending();
}

void completion_register_commands() {
    command_add({.name = "CompletionLast",
        .desc = "Show the last accepted LSP completion",
            .nargs = Nargs::None,
            .run = [](const CmdLine&) {
                buffer_show_text(
                    "[completion]",
                    g_last.empty() ? "nothing accepted yet\n" : g_last);
            }});
}
