#include "complete.hpp"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "buflist.hpp"
#include "columns.hpp"
#include "command.hpp"
#include "core.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "markdown.hpp"
#include "lsp_client.hpp"
#include "lsp_sync.hpp"
#include "multicursor.hpp"
#include "overlay.hpp"
#include "piece.hpp"
#include "snippet.hpp"
#include "syntax.hpp"
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

struct KindLook {
    const char* glyph;
    const char* name;  // override group: ui.menu.kind.<name>
    const char* cap;   // syntax capture whose colour it follows, or nullptr
    uint32_t fg;       // used when cap is null or has no colour in the theme
};

constexpr KindLook kKinds[] = {
    {"\ueb63", "unknown", nullptr, 0x9399b2},                  //  0
    {"\uea93", "text", nullptr, 0xa6adc8},                     //  1
    {"\uea8c", "method", "method", 0x89b4fa},                  //  2
    {"\uea8c", "function", "function", 0x89b4fa},              //  3
    {"\uea8c", "constructor", "constructor", 0x74c7ec},        //  4
    {"\ueb5f", "field", "field", 0xb4befe},                    //  5
    {"\uea88", "variable", "variable", 0xb4befe},              //  6
    {"\ueb5b", "class", "type", 0xf9e2af},                     //  7
    {"\ueb61", "interface", "type", 0xf9e2af},                 //  8
    {"\uea8b", "module", "module", 0xb4befe},                  //  9
    {"\ueb65", "property", "property", 0xb4befe},              // 10
    {"\uea96", "unit", "number", 0xfab387},                    // 11
    {"\uea95", "value", "constant", 0xfab387},                 // 12
    {"\uea95", "enum", "type", 0xf9e2af},                      // 13
    {"\ueb62", "keyword", "keyword", 0xcba6f7},                // 14
    {"\uf0c4", "snippet", nullptr, 0xa6e3a1},                  // 15
    {"\ueb5c", "color", nullptr, 0xf5c2e7},                    // 16
    {"\uea7b", "file", nullptr, 0xa6adc8},                     // 17
    {"\uea94", "reference", nullptr, 0xa6adc8},                // 18
    {"\uea83", "folder", nullptr, 0x89b4fa},                   // 19
    {"\ueb5e", "enum_member", "constant", 0xfab387},           // 20
    {"\ueb5d", "constant", "constant", 0xfab387},              // 21
    {"\uea91", "struct", "type", 0xf9e2af},                    // 22
    {"\uea86", "event", "label", 0x74c7ec},                    // 23
    {"\ueb64", "operator", "operator", 0x89dceb},              // 24
    {"\uea92", "type_parameter", "type.parameter", 0xeba0ac},  // 25
};
static_assert(std::size(kKinds) == 26);

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
std::string g_trace;  // what the last request / reply / key did (CompletionLast)
std::string g_hover_raw;  // raw text of the last hover, for :HoverLast

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

Style pop_base() {
    return theme_ui("ui.menu", Style{.fg = 0xcdd6f4, .bg = 0x181825});
}

Style pop_border(const Style& base) {
    Style b = theme_ui("ui.float.border", Style{.fg = 0x6c7086});
    b.bg = base.bg;
    return b;
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

// The extra cursors are hidden from the server while the main cursor asks for
// completions: the word each extra has typed so far is cut out of the copy
// the server sees, so only the main cursor's surroundings are what it parses.
// A Mask is those [start, end) byte ranges of the real buffer, ascending.
using Mask = std::vector<std::pair<size_t, size_t>>;

// What the reply being parsed was asked against (null: the real buffer).
const Mask* g_unmask = nullptr;

// An LSP position in the masked copy -> an offset in the real buffer. Only
// ranges on the position's own line move its column.
size_t to_off(const PieceTable& pt, int64_t line, int64_t ch, bool utf16) {
    int64_t col = ch;
    if(g_unmask && !g_unmask->empty()) {
        int64_t shift = 0;
        for(const auto& [a, z] : *g_unmask) {
            if(static_cast<int64_t>(pt.line_of(a)) != line) continue;
            const int64_t at =
                static_cast<int64_t>(lsp_col(pt, a, utf16)) - shift;
            if(at >= ch) break;
            shift += static_cast<int64_t>(z - a);
        }
        col += shift;
    }
    return lsp_offset(pt, line, col, utf16);
}

// The ranges to hide: each extra cursor's own word prefix. Left alone are an
// extra inside the main cursor's word (it is the same text), overlapping
// ones, and prefixes with non-ASCII bytes (their columns would not line up).
Mask mask_ranges(const PieceTable& pt, size_t ws, size_t cur) {
    Mask r;
    if(!mc_active()) return r;
    for(const McMark& m : mc_marks()) {
        if(m.pos > pt.size()) continue;
        const size_t s = word_start_at(pt, m.pos);
        if(s >= m.pos || (s <= cur && m.pos >= ws)) continue;
        bool ascii = true;
        for(size_t i = s; i < m.pos && ascii; ++i)
            ascii = static_cast<unsigned char>(pt.at(i)) < 0x80;
        if(ascii) r.emplace_back(s, m.pos);
    }
    std::ranges::sort(r);
    Mask out;
    for(const auto& x : r)
        if(out.empty() || x.first >= out.back().second) out.push_back(x);
    return out;
}

std::vector<Edit> parse_extras(const Json& list, const PieceTable& pt,
                               bool utf16, size_t limit) {
    std::vector<Edit> out;
    if(!list.is_array()) return out;
    for(const Json& e : list.items()) {
        const Json& r = e["range"];
        Edit ed{to_off(pt,
                       r["start"]["line"].as_int(),
                       r["start"]["character"].as_int(),
                       utf16),
                to_off(pt,
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
        it.start = to_off(pt,
                          rg["start"]["line"].as_int(),
                          rg["start"]["character"].as_int(),
                          utf16);
        const size_t end = to_off(pt,
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

// Which bytes of `label` the typed text `q` matched, mirroring rank() above.
std::vector<char> match_hits(std::string_view label, std::string_view q) {
    std::vector<char> hit(label.size(), 0);
    if(q.empty()) return hit;
    const auto ieq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };
    if(label.size() >= q.size() &&
       std::equal(q.begin(), q.end(), label.begin(), ieq)) {
        std::fill_n(hit.begin(), q.size(), 1);
        return hit;
    }
    for(size_t i = 0; i < label.size(); ++i) {
        if(!ieq(label[i], q[0]) || !word_start(label, i)) continue;
        std::vector<size_t> at{i};
        for(size_t j = i + 1; j < label.size() && at.size() < q.size(); ++j)
            if(ieq(label[j], q[at.size()])) at.push_back(j);
        if(at.size() != q.size()) continue;
        for(const size_t p : at) hit[p] = 1;
        return hit;
    }
    return hit;
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

    const bool multi = mc_active();
    // what the user typed of the word, which every cursor replaces
    const auto typed = static_cast<std::ptrdiff_t>(cur) -
                       static_cast<std::ptrdiff_t>(it.start);
    std::string prefix;
    if(multi && typed > 0 && it.start < pt.size())
        prefix = pt.text(it.start, static_cast<size_t>(typed));

    size_t start = 0;
    size_t primary_at = 0;  // anchor on the primary's replaced word
    {
        PieceTable::Group g(pt);
        // The additional edits (auto import) are made once, for the whole
        // pass; the extra cursors are anchors and follow them by themselves,
        // the primary is moved by hand.
        const std::ptrdiff_t shift = apply_extras(pt, std::move(it.extra));
        if(multi)
            set_cursor_offset(static_cast<size_t>(cur + shift));

        // Replace the word before the cursor `at` and put the item in.
        // Returns where the word started.
        const auto put = [&](std::ptrdiff_t at, bool primary) -> size_t {
            const std::ptrdiff_t s0 = std::clamp<std::ptrdiff_t>(
                at - typed, 0, static_cast<std::ptrdiff_t>(pt.size()));
            const auto st = static_cast<size_t>(s0);
            std::ptrdiff_t end = std::clamp<std::ptrdiff_t>(
                at + static_cast<std::ptrdiff_t>(it.tail),
                s0,
                static_cast<std::ptrdiff_t>(pt.size()));
            if(!it.text.empty() && end < static_cast<std::ptrdiff_t>(pt.size()) &&
               pt.at(static_cast<size_t>(end)) == it.text.back() &&
               has_unmatched_tail(it.text))
                ++end;
            const std::string gone =
                end > s0 ? pt.text(st, static_cast<size_t>(end) - st)
                         : std::string();
            if(primary)
                g_last = std::format(
                    "label   : {}\nstart   : {}  tail: {}  cursor: {}  "
                    "extras shift: {}\nreplaced: [{}]\ninserted: [{}]  "
                    "snippet: {}\nraw item: {}\n",
                    it.label, st, it.tail, at, shift, gone, it.text,
                    it.snippet, it.raw.dump());
            if(end > s0) pt.remove(st, static_cast<size_t>(end) - st);
            if(it.snippet && primary) {
                set_cursor_offset(st);
                snippet_expand(it.text);
            } else if(it.snippet) {
                // the others only remember where their tab stops are
                set_cursor_offset(st);
                snippet_peer(it.text);
            } else {
                pt.insert(st, it.text);
                set_cursor_offset(st + it.text.size());
            }
            return st;
        };

        if(!multi) {
            start = put(cur + shift, true);
        } else {
            if(it.snippet) snippet_end();
            mc_each([&] {
                const auto at = static_cast<std::ptrdiff_t>(cursor_offset());
                const bool primary = mc_is_primary();
                // a cursor that did not type the same word is left alone
                const std::ptrdiff_t s0 = at - typed;
                if(!primary && (s0 < 0 || (typed > 0 &&
                    pt.text(static_cast<size_t>(s0),
                            static_cast<size_t>(typed)) != prefix)))
                    return;
                const size_t st = put(at, primary);
                if(primary) primary_at = pt.anchor_add(st, Gravity::Left);
            });
            start = pt.anchor_pos(primary_at).value_or(0);
            pt.anchor_remove(primary_at);
            if(it.snippet) snippet_place_peers();
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

// Precedence, highest first:
//   ui.menu.kind.<name>   per-kind override
//   ui.menu.kind          blanket override (the existing `kov`)
//   syntax capture colour (follows the user's code theme)
//   k.fg                  built-in Catppuccin fallback
Style kind_style(const KindLook& k, const Style& global) {
    Style s = global;
    if(const auto o = theme_override(std::string("ui.menu.kind.") + k.name))
        style_patch(s, *o);
    if(s.fg == 0xFFFFFFFF) {  // nobody chose a colour
        const auto c = k.cap ? syntax_theme(k.cap) : std::nullopt;
        s.fg = c && c->fg != 0xFFFFFFFF ? c->fg : k.fg;
    }
    return s;
}

void paint(OverlayId id, int lw, int dw, int inner, int rows,
           const std::string& typed) {
    const Style base = pop_base();
    overlay_clear(id, base);
    if(S.sel < S.top) S.top = S.sel;
    else if(S.sel >= S.top + static_cast<size_t>(rows))
        S.top = S.sel - rows + 1;

    const Style sel =
        theme_ui("ui.menu.select", Style{.bg = 0x313244, .attrs = ATTR_BOLD});
    const Style bar = theme_ui("ui.menu.marker", Style{.fg = 0xb4befe});
    const Style hit =
        theme_ui("ui.menu.match", Style{.fg = 0xfab387, .attrs = ATTR_BOLD});
    const Style dim = theme_ui("ui.menu.detail", Style{.fg = 0x6c7086});
    const Style kov = theme_ui("ui.menu.kind", Style{});

    const int total = static_cast<int>(S.shown.size());
    const bool scroll = total > rows;
    const int thumb = scroll ? std::max(1, rows * rows / total) : 0;
    const int pos =
        scroll ? static_cast<int>(S.top) * (rows - thumb) / (total - rows) : 0;

    for(int r = 0; r < rows; ++r) {
        const size_t idx = S.top + static_cast<size_t>(r);
        if(idx >= S.shown.size()) break;
        const Item& it = S.items[S.shown[idx]];
        const int y = 1 + r;

        Style row = base;
        if(idx == S.sel) style_patch(row, sel);
        // `row` plus an override, so the selected row's bg shows under every
        // cell
        const auto on = [&row](const Style& over) {
            Style s = row;
            style_patch(s, over);
            return s;
        };

        overlay_put(
            id, 1, y, std::string(static_cast<size_t>(inner), ' '), row);
        if(idx == S.sel) overlay_put(id, 1, y, "\u258c", on(bar));

        const KindLook& k =
            kKinds[it.kind > 0 && it.kind < static_cast<int>(std::size(kKinds))
                       ? it.kind
                       : 0];
        Style ks = on(kind_style(k, kov));
        overlay_put(id, 2, y, k.glyph, ks);

        const std::vector<char> hits = match_hits(it.label, typed);
        int x = 4;
        for(size_t i = 0; i < it.label.size();) {
            const Decoded d = utf8_decode(it.label, i);
            if(x + cell_width(d.cp, 0) > 4 + lw) break;
            x = overlay_put(id,
                            x,
                            y,
                            std::string_view(it.label).substr(i, d.len),
                            hits[i] ? on(hit) : row);
            i += d.len;
        }
        if(dw && !it.detail.empty()) {
            const std::string d = fit(it.detail, dw);
            overlay_put(id, inner - text_width(d), y, d, on(dim));
        }
        if(scroll)
            overlay_put(id,
                        inner,
                        y,
                        r >= pos && r < pos + thumb ? "\u2503" : " ",
                        on(Style{.fg = 0x585b70}));
    }

    if(S.doc_lines.empty()) return;
    Style doc = base;
    style_patch(doc, theme_ui("ui.menu.doc", Style{.fg = 0xa6adc8}));
    for(size_t i = 0; i < S.doc_lines.size(); ++i)
        overlay_put(id,
                    2,
                    2 + rows + static_cast<int>(i),
                    fit(S.doc_lines[i], inner - 2),
                    doc);
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
    o.border_style = pop_border(pop_base());
    if(!S.doc_lines.empty()) o.dividers = {1 + rows};
    o.dismiss_on_move = false;
    o.on_key = popup_key;
    o.on_close = [gen] {
        if(S.gen == gen) reset();
    };
    o.on_paint = [lw, dw, inner, rows, prefix](OverlayId id) {
        paint(id, lw, dw, inner, rows, prefix);
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
              size_t word_start, size_t cur, bool utf16, const Mask& mask,
              const Json& result, const Json& error) {
    // The server got a masked copy: give it the real text back right away.
    if(!mask.empty()) lsp_sync_doc(*buf, true);
    struct Unmask {
        explicit Unmask(const Mask& m) { g_unmask = &m; }
        ~Unmask() { g_unmask = nullptr; }
    } unmask(mask);
    if(seq != g_seq || buf != &E.buf() || E.mode != EditorMode::INSERT) {
        g_trace += std::format("reply #{} dropped: superseded (now #{}) or "
                               "buffer/mode changed\n", seq, g_seq);
        return;
    }
    if(!error.is_null()) {
        g_trace += std::format("reply #{} dropped: server error\n", seq);
        return;
    }
    if(buf->pt.revision() != rev || cursor_offset() != cur) {
        g_trace += std::format(
            "reply #{} dropped: buffer moved on (rev {} -> {}, cursor {} -> "
            "{})\n", seq, rev, buf->pt.revision(), cur, cursor_offset());
        return;
    }
    const Json& list = result.is_array() ? result : result["items"];
    if(!list.is_array()) {
        g_trace += std::format("reply #{} had no item list\n", seq);
        return close();
    }
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
    const bool any = refilter();
    g_trace += std::format(
        "reply #{}: {} items, {} match the prefix at {}..{} -> {}\n", seq,
        S.items.size(), S.shown.size(), word_start, cur,
        any ? "menu shown" : "menu NOT shown");
    if(any) show();
    else close();
}

void request(int kind, const std::string& trigger) {
    if(E.mode != EditorMode::INSERT) return;
    Buffer& b = E.buf();
    const PieceTable& pt = b.pt;
    const size_t cur = cursor_offset();
    const size_t ws = word_start_at(pt, cur);
    Mask mask = mask_ranges(pt, ws, cur);
    const auto doc = mask.empty() ? lsp_sync_doc(b, true) : lsp_sync_mask(b, mask);
    if(!doc || !supports(doc->server)) {
        g_trace += "request skipped: no LSP document or no completion support\n";
        return;
    }
    if(!mask.empty() && !lsp_sync_masked(b)) mask.clear();  // not applied
    // The main cursor's column in the copy the server sees.
    int64_t col = static_cast<int64_t>(lsp_col(pt, cur, doc->utf16));
    for(const auto& [a, z] : mask)
        if(a < ws && pt.line_of(a) == pt.line_of(cur))
            col -= static_cast<int64_t>(z - a);
    Json ctx = Json::obj({{"triggerKind", kind}});
    if(!trigger.empty()) ctx.set("triggerCharacter", trigger);
    const unsigned seq = ++g_seq;
    const uint64_t rev = pt.revision();
    Buffer* buf = &b;
    const bool utf16 = doc->utf16;
    const LspId server = doc->server;
    g_trace = std::format(
        "request #{} sent: line {} col {} (offset {}, word starts {}), extras "
        "active: {}, rev {}\n", seq, pt.line_of(cur), col,
        cur, ws, mc_active(), rev);
    if(!mask.empty())
        g_trace += std::format("  extras hidden from the server: {} word(s)\n",
                               mask.size());
    lsp_request(
        doc->server,
        "textDocument/completion",
        Json::obj({{"textDocument", Json::obj({{"uri", doc->uri}})},
                   {"position",
                    Json::obj({{"line", pt.line_of(cur)},
                               {"character", col}})},
                   {"context", ctx}}),
        [=](const Json& result, const Json& error) {
            on_reply(seq, rev, buf, server, ws, cur, utf16, mask, result,
                     error);
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

namespace {

// The K hover popup. The rows between rule rows are panes (signature, docs,
// ...), each with its own height and scroll. K a second time focuses the
// popup: it turns modal and on_key drives it, vim-window style.
struct HoverPane {
    size_t first = 0, n = 0;  // rows of HoverWin::lines
    int h = 0;                // visible rows
    int top = 0, cur = 0;     // scroll offset and cursor row, pane-relative
    int want = 0;             // wanted cursor column (cells); INT_MAX = line end
};

struct HoverWin {
    unsigned gen = 0;  // tells a stale on_close from the current popup's
    OverlayId id = 0;
    std::vector<MdRow> lines;
    std::vector<HoverPane> panes;
    size_t focus = 0, prev = 0;
    bool focused = false;
    bool cw = false;  // <C-w> pending
    bool g = false;   // first g of gg pending
    Style base, code, head, bul, quo, ring;
};

HoverWin g_hw;
unsigned g_hw_gen = 0;

constexpr size_t kHoverMaxRows = 400;  // layout cap, not a display cap

// First inner row (overlay coordinates) of pane i.
int hover_pane_y(const HoverWin& w, size_t i) {
    int y = 1;
    for(size_t j = 0; j < i; ++j) y += w.panes[j].h + 1;  // +1: the rule
    return y;
}

int hover_row_width(const HoverWin& w, const HoverPane& p) {
    int n = 0;
    for(const MdSeg& s : w.lines[p.first + p.cur].segs) n += text_width(s.text);
    return n;
}

// Cursor column on the current row: the wanted column, clamped to the text.
int hover_col(const HoverWin& w, const HoverPane& p) {
    return std::clamp(p.want, 0, std::max(0, hover_row_width(w, p) - 1));
}

void hover_pane_scroll(HoverPane& p) {
    const int n = static_cast<int>(p.n);
    p.cur = std::clamp(p.cur, 0, n - 1);
    if(p.cur < p.top) p.top = p.cur;
    if(p.cur >= p.top + p.h) p.top = p.cur - p.h + 1;
    p.top = std::clamp(p.top, 0, std::max(0, n - p.h));
}

// Splits at the rules and gives each pane a height. The signature pane gets
// at most 8 rows, the rest share about half the screen.
void hover_split(HoverWin& w) {
    w.panes.clear();
    for(size_t i = 0; i < w.lines.size();) {
        if(w.lines[i].rule) {
            ++i;
            continue;
        }
        HoverPane p;
        p.first = i;
        while(i < w.lines.size() && !w.lines[i].rule) ++i;
        p.n = i - p.first;
        w.panes.push_back(p);
    }
    const int np = static_cast<int>(w.panes.size());
    int remaining =
        std::max({np, 6, E.term_rows / 2 - 2 - std::max(0, np - 1)});
    for(int i = 0; i < np; ++i) {
        HoverPane& p = w.panes[i];
        int cap = std::max(1, remaining - (np - 1 - i) * 3);
        if(i == 0 && np > 1) cap = std::min(cap, 8);
        p.h = std::clamp(static_cast<int>(p.n), 1, cap);
        remaining -= p.h;
    }
}

// Pushes footer, focus ring and hardware cursor to the overlay.
void hover_sync() {
    HoverWin& w = g_hw;
    if(!w.id || w.panes.empty()) return;
    const HoverPane& fp = w.panes[w.focus];
    std::string foot;
    if(w.focused) {
        if(fp.n > static_cast<size_t>(fp.h))
            foot = std::format("{}%", (fp.top + fp.h) * 100 /
                                          static_cast<int>(fp.n));
    } else {
        for(const HoverPane& p : w.panes)
            if(p.n > static_cast<size_t>(p.h)) foot = "\u2026";  // cut short
    }
    const int y = hover_pane_y(w, w.focus);
    const bool focused = w.focused;
    const Style ring = w.ring;
    const int h = fp.h;
    overlay_edit(w.id, [&](OverlaySpec& s) {
        s.footer = foot;
        if(focused) s.focus_span = std::pair{y, y + h}, s.focus_border = ring;
        else s.focus_span.reset();
    });
    if(focused)
        overlay_set_cursor(w.id, 2 + hover_col(w, fp), y + fp.cur - fp.top);
}

void hover_focus() {
    HoverWin& w = g_hw;
    if(!w.id || w.panes.empty()) return;
    w.focused = true;
    w.focus = w.prev = 0;
    w.cw = w.g = false;
    overlay_edit(w.id, [](OverlaySpec& s) {
        s.modal = true;
        s.cursor_block = true;
        s.dismiss_on_move = false;
        s.pass.clear();  // nothing leaks through, ':' included
    });
    hover_sync();
}

// Every key a focused popup gets is consumed, hits and misses alike, so a
// no-op (<C-w>j on the last pane) never falls through to the editor.
bool hover_key(int c) {
    HoverWin& w = g_hw;
    if(!w.id || !w.focused || w.panes.empty()) return false;
    const size_t np = w.panes.size();

    if(w.cw) {
        w.cw = false;  // always cleared, hit or miss
        w.g = false;
        switch(c) {
            case 'j':
                if(w.focus + 1 < np) w.prev = w.focus, ++w.focus;
                break;
            case 'k':
                if(w.focus > 0) w.prev = w.focus, --w.focus;
                break;
            case 'w': w.prev = w.focus, w.focus = (w.focus + 1) % np; break;
            case 'W':
                w.prev = w.focus, w.focus = (w.focus + np - 1) % np;
                break;
            case 'p': std::swap(w.focus, w.prev); break;
            case 'c':
            case 'q': overlay_close(w.id); return true;
            default: break;  // h/l and anything else: no-op
        }
        hover_sync();
        return true;
    }

    HoverPane& p = w.panes[w.focus];
    const bool was_g = std::exchange(w.g, false);
    const int page = std::max(1, p.h), half = std::max(1, p.h / 2);
    int scroll = 0;  // cursor and view move together
    switch(c) {
        case 'q':
        case '\x1b': overlay_close(w.id); return true;
        case CTRL_KEY('w'): w.cw = true; return true;
        case 'j':
        case ARROW_DOWN: ++p.cur; break;
        case 'k':
        case ARROW_UP: --p.cur; break;
        case CTRL_KEY('d'): scroll = half; break;
        case CTRL_KEY('u'): scroll = -half; break;
        case CTRL_KEY('f'):
        case PAGE_DOWN: scroll = page; break;
        case CTRL_KEY('b'):
        case PAGE_UP: scroll = -page; break;
        case 'h':
        case ARROW_LEFT: p.want = std::max(0, hover_col(w, p) - 1); break;
        case 'l':
        case ARROW_RIGHT: p.want = hover_col(w, p) + 1; break;  // clamped
        case '0':
        case HOME: p.want = 0; break;
        case '$':
        case END: p.want = INT_MAX; break;
        case 'G': p.cur = static_cast<int>(p.n) - 1; break;
        case 'g':
            if(was_g) p.cur = 0;
            else w.g = true;
            break;
        default: break;
    }
    p.cur += scroll;
    p.top += scroll;
    hover_pane_scroll(p);
    hover_sync();
    return true;
}

}  // namespace

void hover_show() {
    if(g_hw.id && !g_hw.focused) {  // K on an open popup: take focus
        hover_focus();
        return;
    }
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
            if(g_hw.focused) return;  // don't replace a popup being used
            if(!error.is_null() || !result.is_object()) {
                editor_set_status_message("hover: nothing here");
                return;
            }
            std::string text;
            const Json& c = result["contents"];
            if(c.is_array())
                for(const Json& e : c.items()) text += doc_text(e) + "\n\n";
            else text = doc_text(c);
            g_hover_raw = text;

            auto lines = md_float_layout(
                text, std::max(16, std::min(72, E.term_cols - 8)),
                kHoverMaxRows);
            if(lines.empty()) {
                editor_set_status_message("hover: nothing here");
                return;
            }
            int tw = 12;
            for(const MdRow& l : lines) tw = std::max(tw, l.width);

            const Style base =
                theme_ui("ui.hover", Style{.fg = 0xcdd6f4, .bg = 0x181825});
            Style code = base, head = base, bul = base, quo = base;
            style_patch(bul, md_style(MdRole::Bullet));
            style_patch(quo, md_style(MdRole::Quote));
            style_patch(code,
                        theme_ui("ui.hover.code", md_style(MdRole::Code)));
            style_patch(head,
                        theme_ui("ui.hover.heading",
                                 md_style(MdRole::Heading)));
            Style ring = theme_ui("ui.hover.focus", Style{.fg = 0x89b4fa});
            ring.bg = base.bg;

            if(g_hw.id) overlay_close(g_hw.id);  // its on_close resets g_hw
            HoverWin& w = g_hw;
            w = HoverWin{};
            w.gen = ++g_hw_gen;
            w.lines = std::move(lines);
            w.base = base, w.code = code, w.head = head, w.bul = bul;
            w.quo = quo, w.ring = ring;
            hover_split(w);

            // a rule sits on the row after each pane but the last
            std::vector<int> dividers;
            for(size_t i = 0; i + 1 < w.panes.size(); ++i)
                dividers.push_back(hover_pane_y(w, i) + w.panes[i].h);

            OverlaySpec o;
            o.at = OverlaySpec::At::Cursor;
            o.x = 0;
            o.y = 1;
            o.w = tw + 4;
            o.h = hover_pane_y(w, w.panes.size());  // panes, rules, frame
            o.z = 50;
            o.framed = true;
            o.border_style = pop_border(base);
            o.dividers = std::move(dividers);
            o.on_key = hover_key;
            o.on_close = [gen = w.gen] {
                if(g_hw.gen == gen) g_hw = HoverWin{};
            };
            o.on_paint = [](OverlayId id) {
                const HoverWin& w = g_hw;
                if(w.id != id) return;
                overlay_clear(id, w.base);
                for(size_t pi = 0; pi < w.panes.size(); ++pi) {
                    const HoverPane& p = w.panes[pi];
                    const int y0 = hover_pane_y(w, pi);
                    for(int r = 0; r < p.h; ++r) {
                        const size_t li = p.first + p.top + r;
                        if(li >= p.first + p.n) break;
                        int x = 2;
                        for(const MdSeg& s : w.lines[li].segs) {
                            Style st = s.tone == MdTone::Code      ? w.code
                                       : s.tone == MdTone::Heading ? w.head
                                       : s.tone == MdTone::Bullet  ? w.bul
                                       : s.tone == MdTone::Quote   ? w.quo
                                                                   : w.base;
                            if(s.style) style_patch(st, *s.style);
                            x = overlay_put(id, x, y0 + r, s.text, st);
                        }
                    }
                }
            };
            w.id = overlay_open(std::move(o));
            hover_sync();
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
    g_trace = std::format("key {}: no request (prefix {} < {} or not a word "
                          "key)\n", c, cur - ws, kMinPrefix);
    cancel_pending();
}

void completion_register_commands() {
    command_add({.name = "CompletionLast",
                 .desc = "Show the last accepted LSP completion",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) {
                     buffer_show_text(
                         "[completion]",
                         (g_last.empty() ? std::string("nothing accepted yet\n")
                                         : g_last) +
                             "\n-- last request --\n" +
                             (g_trace.empty() ? std::string("none yet\n")
                                              : g_trace));
                 }});
    command_add({.name = "HoverLast",
                 .desc = "Show the raw text of the last LSP hover",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) {
                     buffer_show_text(
                         "[hover]",
                         g_hover_raw.empty() ? "no hover yet\n" : g_hover_raw);
                 }});
}
