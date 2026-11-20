// :Doc, :DocInstall, :DocUpdate. Editor half; see doc_core.hpp for the rest.
#include "doc.hpp"
#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "buflist.hpp"
#include "command.hpp"
#include "doc_ast.hpp"
#include "doc_core.hpp"
#include "dock.hpp"
#include "editor.hpp"
#include "job.hpp"
#include "options.hpp"
#include "parser.hpp"
#include "picker.hpp"
#include "piece.hpp"
#include "syntax.hpp"
#include "term_panel.hpp"
#include "window.hpp"
#include "window_ed.hpp"

namespace fs = std::filesystem;

namespace {

using namespace std::string_view_literals;

// ---- state ------------------------------------------------------------------

std::string docs_root() { return parser_data_dir() + "/docs"; }

std::string env_or(const char* k, const char* def) {
    const char* v = std::getenv(k);
    return v && *v ? v : def;
}

bool read_file(const std::string& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if(!in) return false;
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

std::string trim(std::string_view s) {
    while(!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while(!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return std::string(s);
}

std::string last_line(std::string_view s) {
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.remove_suffix(1);
    const size_t nl = s.rfind('\n');
    return std::string(nl == std::string_view::npos ? s : s.substr(nl + 1));
}

// Indexes are loaded the first time a lookup needs them: index.json for cpp is
// a few MB, so :DocInstall'ed docsets cost nothing until :Doc is used.
std::map<std::string, doc::Index> g_idx;
// last scope segment -> entries ("size" -> std::vector::size, std::string::size
// ...), per docset, built the first time a member lookup needs it. Views point
// into g_idx, so the two are always cleared together.
std::map<std::string, std::unordered_multimap<std::string_view, const doc::Entry*>>
    g_suffix;

const doc::Index* index_for(const std::string& slug) {
    if(const auto it = g_idx.find(slug); it != g_idx.end()) return &it->second;
    doc::Index ix;
    ix.slug = slug;
    if(!doc::load_index(docs_root() + "/" + slug + "/index.tsv", ix))
        return nullptr;
    return &(g_idx[slug] = std::move(ix));
}

// the one reused doc split
uint64_t g_buf_id = 0;
int g_win_id = 0;
std::string g_file;       // root-relative path of the file shown ("cpp/a/b.md")
std::string g_last_slug;  // the docset of the last lookup: active while inside
                          // the split

bool is_doc_buffer(const Buffer& b) { return g_buf_id && b.id == g_buf_id; }

std::string slug_of(const std::string& rel) {
    return rel.substr(0, rel.find('/'));
}

// ---- docsets ----------------------------------------------------------------

std::vector<std::string> installed_slugs() {
    std::vector<std::string> out;
    for(const doc::DocsetInfo& d : doc::installed(docs_root()))
        out.push_back(d.slug);
    return out;
}

// "lua" -> "lua~5.4" when only that one is installed
std::string installed_match(const std::string& want) {
    const std::vector<std::string> all = installed_slugs();
    if(std::ranges::find(all, want) != all.end()) return want;
    std::string best;
    for(const std::string& s : all)
        if(doc::slug_base(s) == want && s > best) best = s;
    return best;
}

std::string current_ft() {
    const Buffer& b = E.buf();
    if(is_doc_buffer(b)) return {};
    return std::string(b.syntax.lang());
}

// Installed docsets in search order: the buffer's filetype first.
std::vector<std::string> search_order(std::string* hint = nullptr) {
    std::vector<std::string> all = installed_slugs(), order;
    const auto take = [&](const std::string& s) {
        if(std::ranges::find(order, s) == order.end()) order.push_back(s);
    };
    if(!g_last_slug.empty() && is_doc_buffer(E.buf()) &&
       std::ranges::find(all, g_last_slug) != all.end())
        take(g_last_slug);
    const std::string ft = current_ft();
    const auto bases = doc::bases_for_filetype(ft);
    bool found = false;
    for(const std::string_view base : bases) {
        std::vector<std::string> m;
        for(const std::string& s : all)
            if(doc::slug_base(s) == base) m.push_back(s);
        std::ranges::sort(m, std::greater<>());  // newest version first
        for(const std::string& s : m) take(s), found = true;
    }
    if(hint && !bases.empty() && !found)
        *hint = "no " + std::string(bases.front()) + " docset: :DocInstall " +
                std::string(bases.front());
    for(const std::string& s : all) take(s);
    return order;
}

// ---- the doc split ----------------------------------------------------------

Buffer* doc_buffer() {
    if(g_buf_id)
        for(const auto& b : E.buffers)
            if(b->id == g_buf_id) return b.get();
    auto b = std::make_unique<Buffer>();
    b->nofile = true;
    g_buf_id = b->id;
    E.buffers.push_back(std::move(b));
    return E.buffers.back().get();
}

void fill(Buffer& b, const std::string& text, const std::string& name) {
    b.filename = name;  // ends in .md: that is what turns markdown rendering on
    b.nofile = true;
    b.orig = text;
    b.eol = text.empty() || text.back() == '\n';
    std::string_view v = b.orig;
    if(!b.orig.empty() && b.orig.back() == '\n') v.remove_suffix(1);
    b.pt = PieceTable(v);
    b.wire();
    b.pt.locked = true;  // read-only
    b.cx = b.cy = b.want_rx = b.row_offset = b.col_offset = 0;
}

// Brings the doc split to the front (making it the first time) and puts the
// text in it.
bool present(const std::string& name, const std::string& text) {
    windows_ensure();
    win::Win* dw = nullptr;
    for(win::Win* w : win::all())
        if(w->id == g_win_id) dw = w;
    if(dw) {
        win_focus_window(dw);
    } else {
        if(!win_split(false, false)) {
            editor_set_status_message("doc: can't open a split");
            return false;
        }
        g_win_id = win::focus()->id;
    }
    Buffer* b = doc_buffer();
    if(&E.buf() != b) {
        for(size_t i = 0; i < E.buffers.size(); ++i)
            if(E.buffers[i].get() == b) buffer_switch(i);
    }
    fill(*b, text, name);
    E.cx = E.cy = E.want_rx = E.row_offset = E.col_offset = E.row_sub = 0;
    return true;
}

void jump_to_anchor(std::string_view anchor) {
    if(anchor.empty()) return;
    Buffer& b = E.buf();
    const int line = doc::find_anchor(b.pt.text(), anchor);
    if(line < 0) return;
    set_cursor_offset(b.pt.line_start(static_cast<size_t>(line)));
}

// `rel` is relative to the docs root: "index.md", "cpp/INDEX.md", "cpp/a/b.md".
// Returns false (with a status message) when it cannot be shown.
bool show(const std::string& rel,
          std::string_view anchor,
          const std::string& recent_name) {
    const fs::path root = docs_root();
    std::error_code ec;
    const fs::path abs = (root / rel).lexically_normal();
    const fs::path r = abs.lexically_relative(root);
    if(r.empty() || *r.begin() == "..") {
        editor_set_status_message("doc: {} is outside the docs directory", rel);
        return false;
    }
    std::string text;
    if(!read_file(abs.string(), text)) {
        editor_set_status_message("doc: can't read {}", r.string());
        return false;
    }
    const std::string rs = r.generic_string();
    if(rs == "index.md")
        text = doc::splice_recent(text, doc::load_recent(root.string()));
    else if(rs.find('/') != std::string::npos && !rs.ends_with("/INDEX.md")) {
        const int w = opts.doc_table_width > 0 ? opts.doc_table_width
                                               : std::max(E.term_cols - 8, 40);
        text = doc::reflow_tables(text, w);
        if(opts.doc_breaks) text = doc::semantic_breaks(text);
    }
    if(!present("[doc] " + rs, text)) return false;
    g_file = rs;
    const std::string slug = slug_of(rs);
    if(rs.find('/') != std::string::npos) g_last_slug = slug;
    jump_to_anchor(anchor);
    if(!recent_name.empty() && rs.find('/') != std::string::npos &&
       !rs.ends_with("/INDEX.md")) {
        std::string target = rs.substr(slug.size() + 1);
        if(!anchor.empty()) target += "#" + std::string(anchor);
        doc::note_recent(root.string(), {slug, recent_name, target});
    }
    return true;
}

// ---- saved html in w3m ------------------------------------------------------

bool have_exe(const char* exe) {
    const char* path = std::getenv("PATH");
    if(!path) return false;
    std::string_view rest = path;
    while(!rest.empty()) {
        const size_t c = rest.find(':');
        const std::string dir(rest.substr(0, c));
        if(!dir.empty() && access((dir + "/" + exe).c_str(), X_OK) == 0)
            return true;
        if(c == std::string_view::npos) break;
        rest.remove_prefix(c + 1);
    }
    return false;
}

void open_html_file(const std::string& rel_html,
                    const std::string& frag,
                    const std::string& what) {
    const fs::path p = fs::path(docs_root()) / rel_html;
    std::error_code ec;
    if(!fs::exists(p, ec)) {
        editor_set_status_message("doc: no saved HTML for {}: :set save_html, "
                                  "then :DocUpdate! to create it",
                                  what);
        return;
    }
    if(!have_exe("w3m")) {
        editor_set_status_message("doc: w3m is not installed");
        return;
    }
    std::string arg = p.string();
    if(!frag.empty()) arg += "#" + frag;
    const PanelId id = term_open("", {"w3m", arg});
    dock_show(id, OutMode::Bottom);
    dock_focus(true);
}

// "cpp/a/b.md#x" (root-relative, from a doc target) -> saved html
void open_html_target(const std::string& slug, const std::string& target) {
    std::string stem = target, frag;
    if(const size_t h = stem.find('#'); h != std::string::npos) {
        frag = doc::pct_encode(stem.substr(h + 1));
        stem.resize(h);
    }
    if(stem.ends_with(".md")) stem.resize(stem.size() - 3);
    open_html_file(slug + "/html/" + stem + ".html", frag, slug);
}

// ---- opening an entry -------------------------------------------------------

void open_entry(const std::string& slug, const doc::Entry& e, bool html) {
    if(html) {
        open_html_target(slug, e.target);
        return;
    }
    std::string path = e.target, anchor;
    if(const size_t h = path.find('#'); h != std::string::npos) {
        anchor = path.substr(h + 1);
        path.resize(h);
    }
    if(show(slug + "/" + path, anchor, e.name))
        editor_set_status_message("doc: {} ({})", e.name, slug);
}

void open_picker(std::vector<PickItem> items, std::string query, bool html) {
    PickerSpec s;
    s.prompt = "Doc> ";
    s.fuzzy = true;
    s.max_results = 200;
    s.query = std::move(query);
    s.on_accept = [html](const PickItem& it) {
        const size_t sl = it.path.find('/');
        if(sl == std::string::npos) return;
        const doc::Entry e{it.text, it.path.substr(sl + 1), {}};
        open_entry(it.path.substr(0, sl), e, html);
    };
    picker_open(std::move(s));
    picker_add(std::move(items));
}

PickItem pick_item(const std::string& slug, const doc::Entry& e) {
    return {.text = e.name,
            .detail = slug + " \xC2\xB7 " + e.type,
            .path = slug + "/" + e.target};
}

// Names are stored with the docset's separators, so a seed like "v.size" can
// never match "std::vector::size" in the picker: seed with the last segment.
std::string seed_for(std::string_view tok) {
    size_t cut = 0;
    for(size_t i = 0; i < tok.size(); ++i) {
        if(tok[i] == '.') cut = i + 1;
        else if(tok.compare(i, 2, "::") == 0 || tok.compare(i, 2, "->") == 0)
            cut = i + 2;
    }
    return std::string(tok.substr(cut));
}

void no_docsets(const std::string& hint) {
    editor_set_status_message("doc: no docsets installed: :DocInstall {}",
                              hint.empty() ? "cpp" : hint.substr(hint.rfind(' ') + 1));
}

// One name against the docsets in `order`. A single hit opens the page, several
// (overloads, one name twice in a docset) open the picker; false when nothing
// matches. `raw` skips separator rewriting; `shorten` retries with one scope
// less at a time (fs::path::filename -> fs::path).
bool resolve(const std::string& name,
             bool raw,
             bool shorten,
             const std::vector<std::string>& order,
             const std::string& hint,
             const std::string& shown,
             bool html) {
    for(size_t depth = 0;; ++depth) {
        if(depth > 0 && !shorten) return false;
        bool any = false;
        for(const std::string& slug : order) {
            const doc::Index* ix = index_for(slug);
            if(!ix) continue;
            const std::string_view sep = doc::lang_for(slug).sep;
            const std::vector<std::string> prefixes =
                raw ? std::vector<std::string>{name}
                    : doc::scope_prefixes(doc::normalize_name(name, sep), sep);
            if(depth >= prefixes.size()) continue;
            any = true;
            std::vector<const doc::Entry*> hits =
                doc::find_exact(*ix, prefixes[depth]);
            std::set<std::string> seen;
            std::erase_if(hits, [&](const doc::Entry* e) {
                return !seen.insert(e->target).second;
            });
            if(hits.empty()) continue;
            if(hits.size() == 1) {
                open_entry(slug, *hits[0], html);
                if(depth > 0 && !html)
                    editor_set_status_message("doc: {} ({}): no entry for {}",
                                              hits[0]->name,
                                              slug,
                                              shown);
                else if(!hint.empty() && !html)
                    editor_set_status_message(
                        "doc: {} ({}); {}", hits[0]->name, slug, hint);
                return true;
            }
            std::vector<PickItem> items;
            for(const doc::Entry* e : hits) items.push_back(pick_item(slug, *e));
            open_picker(std::move(items), prefixes[depth], html);
            return true;
        }
        if(!any) return false;
    }
}

// A miss is never a dead end: everything installed, seeded with `seed`.
void full_picker(const std::vector<std::string>& order,
                 const std::string& seed,
                 const std::string& hint,
                 bool html) {
    std::vector<PickItem> items;
    for(const std::string& slug : order)
        if(const doc::Index* ix = index_for(slug))
            for(const doc::Entry& e : ix->entries)
                items.push_back(pick_item(slug, e));
    open_picker(std::move(items), seed, html);
    if(!hint.empty()) editor_set_status_message("{}", hint);
}

// Member access with an unknown receiver: every entry whose last scope segment
// is `member`, from the first docset that has any. One opens directly, several
// open a picker holding only those, not the whole index.
bool narrow_member(const std::string& member,
                   const std::vector<std::string>& order,
                   bool html) {
    for(const std::string& slug : order) {
        const doc::Index* ix = index_for(slug);
        if(!ix) continue;
        auto [it, fresh] = g_suffix.try_emplace(slug);
        if(fresh) {
            const std::string_view sep = doc::lang_for(slug).sep;
            for(const doc::Entry& e : ix->entries) {
                const size_t p = e.name.rfind(sep);
                if(p == std::string::npos) continue;  // members only
                it->second.emplace(std::string_view(e.name).substr(p + sep.size()),
                                   &e);
            }
        }
        std::vector<const doc::Entry*> hits;
        std::set<std::string> seen;
        const auto [lo, hi] = it->second.equal_range(member);
        for(auto i = lo; i != hi; ++i)
            if(seen.insert(i->second->target).second) hits.push_back(i->second);
        if(hits.empty()) continue;
        if(hits.size() == 1) {
            open_entry(slug, *hits[0], html);
            return true;
        }
        std::vector<PickItem> items;
        for(const doc::Entry* e : hits) items.push_back(pick_item(slug, *e));
        open_picker(std::move(items), member, html);
        return true;
    }
    return false;
}

// Plain text lookup: ":Doc fs::path::filename", or the text token under the
// cursor when the syntax tree has nothing to say.
void lookup(const std::string& token, bool html) {
    std::string hint;
    const std::vector<std::string> order = search_order(&hint);
    if(order.empty()) {
        no_docsets(hint);
        return;
    }
    if(resolve(token, false, true, order, hint, token, html)) return;
    full_picker(order, seed_for(token), hint, html);
}

// Syntax-tree lookup. Exact candidates first (the AST said what to look for),
// then the as-written name with scopes shortened, then members by last
// segment, then the picker.
void lookup_query(const doc::Query& q, bool html) {
    std::string hint;
    const std::vector<std::string> order = search_order(&hint);
    if(order.empty()) {
        no_docsets(hint);
        return;
    }
    for(const doc::Cand& c : q.cands)
        if(resolve(c.name, c.raw, false, order, hint, c.name, html)) return;
    if(q.member.empty() && !q.cands.empty() && !q.cands.front().raw &&
       q.cands.front().name.find('.') != std::string::npos &&
       resolve(q.cands.front().name, false, true, order, hint,
               q.cands.front().name, html))
        return;
    if(!q.member.empty() && narrow_member(q.member, order, html)) return;
    full_picker(order, q.seed, hint, html);
}

// The query for the name under the cursor, from the buffer's syntax tree.
std::optional<doc::Query> ast_query() {
    Buffer& b = E.buf();
    if(is_doc_buffer(b)) return std::nullopt;
    if(E.cy < 0 || static_cast<size_t>(E.cy) >= b.pt.line_count())
        return std::nullopt;
    b.syntax.update(b.pt, b.eol);  // the renderer's own call: a no-op when current
    const TSTree* tree = b.syntax.tree();
    if(!tree) return std::nullopt;
    const PieceTable* pt = &b.pt;
    const doc::TextFn text = [pt](size_t a, size_t e) {
        const size_t n = pt->size();  // the tree may count a final newline
        a = std::min(a, n);
        e = std::min(e, n);
        return a < e ? pt->text(a, e - a) : std::string();
    };
    const size_t byte =
        b.pt.line_start(static_cast<size_t>(E.cy)) + static_cast<size_t>(E.cx);
    return doc::query_at(tree, b.syntax.lang(), text, byte);
}

std::string cursor_token() {
    const Buffer& b = E.buf();
    if(E.cy < 0 || static_cast<size_t>(E.cy) >= b.pt.line_count()) return {};
    return doc::token_at(b.pt.line_text(static_cast<size_t>(E.cy)),
                         static_cast<size_t>(E.cx));
}

void show_index(const std::string& arg, bool html) {
    const fs::path root = docs_root();
    if(doc::installed(root.string()).empty()) {
        std::string hint;
        search_order(&hint);
        editor_set_status_message(
            "doc: no docsets installed: :DocInstall {}",
            hint.empty() ? "cpp" : hint.substr(hint.rfind(' ') + 1));
        return;
    }
    std::string slug;
    if(!arg.empty()) {
        slug = installed_match(arg);
        if(slug.empty()) {
            editor_set_status_message(
                "doc: {} is not installed: :DocInstall {}", arg, arg);
            return;
        }
    }
    if(html) {
        open_html_file(slug.empty() ? "index.html" : slug + "/INDEX.html",
                       "",
                       slug.empty() ? "the index" : slug);
        return;
    }
    show(slug.empty() ? "index.md" : slug + "/INDEX.md", "", "");
}

void cmd_doc(const CmdLine& c) {
    const std::string a = trim(c.args);
    if(a == "show" || a.starts_with("show ")) {
        show_index(trim(std::string_view(a).substr(4)), c.bang);
        return;
    }
    if(!a.empty()) {
        lookup(a, c.bang);
        return;
    }
    if(const auto q = ast_query()) {
        lookup_query(*q, c.bang);
        return;
    }
    const std::string tok = cursor_token();
    if(tok.empty())
        show_index("", c.bang);  // not on a name: the index, never an error
    else lookup(tok, c.bang);
}

std::vector<CmdCand> complete_doc(std::string_view lead) {
    std::vector<CmdCand> out;
    if("show"sv.starts_with(lead)) out.push_back({"show", "index"});
    const std::vector<std::string> order = search_order();
    if(order.empty() || lead.empty()) return out;
    if(const doc::Index* ix = index_for(order.front())) {
        const auto lo =
            std::lower_bound(ix->entries.begin(),
                             ix->entries.end(),
                             lead,
                             [](const doc::Entry& e, std::string_view n) {
                                 return std::string_view(e.name) < n;
                             });
        for(auto it = lo; it != ix->entries.end() &&
                          it->name.starts_with(lead) && out.size() < 80;
            ++it)
            out.push_back({it->name, it->type});
    }
    return out;
}

// ---- install / update -------------------------------------------------------

struct Run {
    std::deque<std::string> todo;
    bool update = false, force = false;
    int ok = 0, bad = 0, total = 0;
    std::string label;  // "DocInstall" / "DocUpdate"
};
std::optional<Run> g_run;
// Queue entry that only refreshes the DevDocs catalogue (not a docset name).
constexpr const char* kCatalogJob = "@catalog";

void run_next() {
    if(!g_run) return;
    if(g_run->todo.empty()) {
        const Run r = *g_run;
        g_run.reset();
        g_idx.clear();  // indexes may have been replaced
        g_suffix.clear();
        if(r.total > 1)
            editor_set_status_message(
                "{}: {} done, {} failed", r.label, r.ok, r.bad);
        return;
    }
    const std::string slug = g_run->todo.front();
    g_run->todo.pop_front();

    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if(ec) {
        editor_set_status_message("{}: can't locate the winter binary",
                                  g_run->label);
        g_run.reset();
        return;
    }
    doc::ScriptOpts so;
    so.exe = exe.string();
    so.slug = slug;
    so.root = docs_root();
    so.cdn = env_or("WINTER_DOCS_CDN", "https://documents.devdocs.io");
    so.manifest =
        env_or("WINTER_DOCS_MANIFEST", "https://devdocs.io/docs.json");
    so.html = opts.save_html;
    so.update = g_run->update;
    so.force = g_run->force;
    const std::string label = g_run->label;
    const JobId id = job_start(
        {.cmd = slug == kCatalogJob ? doc::catalog_script(so)
                                    : doc::install_script(so),
         .on_data =
             [label](std::string_view chunk) {
                 const std::string l = last_line(chunk);
                 if(!l.empty()) editor_set_status_message("{}: {}", label, l);
             },
         .on_exit =
             [label](JobResult r) {
                 if(!g_run) return;
                 std::string msg = last_line(r.out);
                 const bool ok = r.code == 0;
                 if(!ok) {
                     if(msg.starts_with("FAILED: ")) msg.erase(0, 8);
                     if(msg.empty())
                         msg = "exit code " + std::to_string(r.code);
                     editor_set_status_message("{} failed: {}", label, msg);
                 } else {
                     editor_set_status_message("{}: {}", label, msg);
                 }
                 (ok ? g_run->ok : g_run->bad)++;
                 run_next();
             }});
    if(!id) {
        editor_set_status_message("{}: can't start the job", label);
        ++g_run->bad;
        run_next();
    }
}

void start_run(std::vector<std::string> slugs,
               bool update,
               bool force,
               const char* label) {
    if(g_run) {
        editor_set_status_message("{}: a job is already running", g_run->label);
        return;
    }
    Run r;
    r.update = update;
    r.force = force;
    r.label = label;
    r.total = static_cast<int>(slugs.size());
    for(std::string& s : slugs) r.todo.push_back(std::move(s));
    g_run = std::move(r);
    run_next();
}

// Installed, going by base name: "lua" counts when only lua~5.4 is present.
bool is_installed(std::string_view base) {
    return !installed_match(std::string(base)).empty();
}

void cmd_docinstall(const CmdLine& c) {
    if(c.argv.empty()) {
        std::string have;
        for(const doc::Docset* d : doc::all_docsets()) {
            have += (have.empty() ? "" : " ") + std::string(d->slug);
            if(is_installed(d->slug)) have += '*';
        }
        for(const std::string& s : installed_slugs())
            if(!doc::docset_for(doc::slug_base(s)))
                have += " " + s + "*";
        std::set<std::string> bases;
        for(const doc::Avail& a : doc::load_catalog(docs_root()))
            bases.insert(doc::slug_base(a.slug));
        size_t more = 0;
        for(const std::string& b : bases)
            if(!doc::docset_for(b)) ++more;
        editor_set_status_message(
            "Docsets (* installed; 'all' = these): {}  +{}",
            have,
            bases.empty() ? std::string("(run :DocUpdate for the DevDocs list)")
                          : std::to_string(more) + " more from DevDocs, Tab completes them");
        return;
    }
    std::vector<std::string> slugs;
    for(const std::string& s : c.argv) {
        if(s == "all") {  // the registry; ! also redoes the installed ones
            for(const doc::Docset* d : doc::all_docsets())
                if(c.bang || !is_installed(d->slug))
                    slugs.emplace_back(d->slug);
            continue;
        }
        if(!doc::valid_slug(s)) {
            editor_set_status_message("DocInstall: '{}' is not a docset name",
                                      s);
            return;
        }
        slugs.push_back(s);
    }
    if(slugs.empty()) {
        editor_set_status_message("DocInstall: everything is installed");
        return;
    }
    start_run(std::move(slugs), false, c.bang, "DocInstall");
}

void cmd_docupdate(const CmdLine& c) {
    std::vector<std::string> slugs;
    if(c.argv.empty()) {
        slugs = installed_slugs();
    } else {
        for(const std::string& s : c.argv) {
            const std::string m = installed_match(s);
            if(m.empty()) {
                editor_set_status_message("DocUpdate: {} is not installed", s);
                return;
            }
            slugs.push_back(m);
        }
    }
    if(slugs.empty()) slugs.push_back(kCatalogJob);  // still refresh the list
    start_run(std::move(slugs), true, c.bang, "DocUpdate");
}

// One entry per docset. Versioned slugs ("lua~5.3") are only offered once the
// word being typed contains a '~', so `lua~<Tab>` lists that docset's versions.
std::vector<CmdCand> complete_install(std::string_view lead) {
    std::vector<CmdCand> out;
    std::set<std::string> seen;
    const auto add = [&](const std::string& slug, const std::string& label) {
        if(slug.starts_with(lead) && seen.insert(slug).second)
            out.push_back({slug, label});
    };
    const std::vector<std::string> inst = installed_slugs();
    const std::set<std::string> have(inst.begin(), inst.end());
    const std::vector<doc::Avail> cat = doc::load_catalog(docs_root());

    if(lead.find('~') != std::string_view::npos) {
        for(const doc::Avail& a : cat)
            if(a.slug.find('~') != std::string::npos)
                add(a.slug,
                    have.contains(a.slug)
                        ? "installed"
                        : a.name + (a.version.empty() ? "" : " " + a.version));
        for(const std::string& s : inst) add(s, "installed");
        return out;
    }

    std::map<std::string, int> versions;  // base -> how many versions exist
    for(const doc::Avail& a : cat)
        if(a.slug.find('~') != std::string::npos)
            ++versions[doc::slug_base(a.slug)];
    // "installed" or the name, plus a pointer to the versions when there are any
    const auto label = [&](const std::string& base, const std::string& name) {
        std::string l = is_installed(base) ? "installed" : name;
        if(const auto v = versions.find(base); v != versions.end())
            l += " (" + std::to_string(v->second) + " versions: " + base + "~)";
        return l;
    };

    add("all", "every docset in the registry");
    // winter's own rows first, then the rest of DevDocs
    for(const doc::Docset* d : doc::all_docsets())
        add(std::string(d->slug), label(std::string(d->slug), std::string(d->name)));
    for(const doc::Avail& a : cat) {
        const std::string base = doc::slug_base(a.slug);
        add(base, label(base, a.name));
    }
    for(const std::string& s : inst)
        if(s.find('~') == std::string::npos) add(s, "installed");
    return out;
}

std::vector<CmdCand> complete_update(std::string_view) {
    std::vector<CmdCand> out;
    for(const std::string& s : installed_slugs())
        out.push_back({s, "installed"});
    return out;
}

// ---- K on a link ------------------------------------------------------------

struct Link {
    size_t lo = 0, hi = 0;  // byte range of the whole [text](url) in the line
    std::string text, url;
};

std::vector<Link> links_in(std::string_view line) {
    std::vector<Link> out;
    size_t from = 0;
    while(true) {
        const size_t mid = line.find("](", from);
        if(mid == std::string_view::npos) break;
        const size_t end = line.find(')', mid + 2);
        if(end == std::string_view::npos) break;
        size_t lb = mid;
        while(lb > 0) {
            --lb;
            if(line[lb] == '[' && (lb == 0 || line[lb - 1] != '\\')) break;
            if(lb == 0) {
                lb = std::string_view::npos;
                break;
            }
        }
        from = end + 1;
        if(lb == std::string_view::npos || line[lb] != '[') continue;
        out.push_back({lb,
                       end + 1,
                       std::string(line.substr(lb + 1, mid - lb - 1)),
                       std::string(line.substr(mid + 2, end - mid - 2))});
    }
    return out;
}

std::string unescape_md(std::string_view s) {
    std::string o;
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == '\\' && i + 1 < s.size()) ++i;
        o += s[i];
    }
    return o;
}

}  // namespace

bool doc_follow_link() {
    Buffer& b = E.buf();
    if(!is_doc_buffer(b)) return false;
    if(E.cy < 0 || static_cast<size_t>(E.cy) >= b.pt.line_count()) return true;
    const std::string line = b.pt.line_text(static_cast<size_t>(E.cy));
    const std::vector<Link> links = links_in(line);
    const Link* pick = nullptr;
    for(const Link& l : links) {
        if(static_cast<size_t>(E.cx) >= l.lo &&
           static_cast<size_t>(E.cx) < l.hi) {
            pick = &l;
            break;
        }
        if(!pick) pick = &l;  // else the first one on the row
    }
    if(!pick) {
        editor_set_status_message("doc: no link on this line");
        return true;
    }
    const std::string url = pick->url;
    if(url.starts_with("http:") || url.starts_with("https:") ||
       url.starts_with("mailto:")) {
        editor_set_status_message("doc: external link: {}",
                                  doc::pct_decode(url));
        return true;
    }
    if(url.starts_with("#")) {  // same page
        const std::string frag = doc::pct_decode(url.substr(1));
        if(doc::find_anchor(b.pt.text(), frag) < 0)
            editor_set_status_message("doc: no heading for #{}", frag);
        else jump_to_anchor(frag);
        return true;
    }
    std::string path = url, frag;
    if(const size_t h = path.find('#'); h != std::string::npos) {
        frag = doc::pct_decode(path.substr(h + 1));
        path.resize(h);
    }
    path = doc::pct_decode(path);
    const fs::path dir = fs::path(g_file).parent_path();
    const std::string rel = (dir / path).lexically_normal().generic_string();
    if(rel.empty() || rel.starts_with("..") || rel.starts_with("/")) {
        editor_set_status_message("doc: link leaves the docs directory");
        return true;
    }
    const std::string name = unescape_md(pick->text);
    show(rel, frag, name);
    return true;
}

void doc_register_commands() {
    command_add({.name = "Doc",
                 .desc = "Offline documentation for the name under the cursor "
                         "(! = saved HTML)",
                 .usage = ":Doc[!] [name | show [slug]]",
                 .bang = true,
                 .nargs = Nargs::Opt,
                 .raw = true,
                 .complete = complete_doc,
                 .run = cmd_doc});
    command_add({.name = "DocInstall",
                 .desc = "Download and convert docsets for offline use (all = the registry)",
                 .usage = ":DocInstall[!] [slug... | all]",
                 .bang = true,
                 .nargs = Nargs::Any,
                 .complete = complete_install,
                 .run = cmd_docinstall});
    command_add(
        {.name = "DocUpdate",
         .desc = "Refresh the DevDocs list and installed docsets and their indexes",
         .usage = ":DocUpdate[!] [slug]",
         .bang = true,
         .nargs = Nargs::Any,
         .complete = complete_update,
         .run = cmd_docupdate});
}
