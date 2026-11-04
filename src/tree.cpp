#include "tree.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "buffer.hpp"
#include "buflist.hpp"
#include "columns.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "glyphs.hpp"
#include "inputbox.hpp"
#include "job.hpp"
#include "keys.hpp"
#include "loop.hpp"
#include "normal.hpp"
#include "overlay.hpp"
#include "registry.hpp"
#include "theme.hpp"
#include "virt.hpp"
#include "watch.hpp"

namespace fs = std::filesystem;

void load_git_status();  // defined below at global scope

namespace {

struct Node {
    std::string name;
    fs::path path;
    bool dir = false, open = false, loaded = false, link;
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> kids;
};

struct Row {
    Node* n;
    int depth;
    std::string guide;
};

struct Entry {
    std::string name;
    bool dir, link;
};

std::unique_ptr<Node> g_root;
std::vector<Row> g_rows;
std::vector<TreeLine> g_lines;
size_t g_sel = 0, g_top = 0;
bool g_shown = false, g_hidden = false;
int g_cols = 0;

bool focused() { return E.mode == FILES; }
void focus(bool on) {
    if(on) set_mode(FILES);
    else if(E.mode == FILES) set_mode(NORMAL);
}

std::string lower(std::string s) {
    for(char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<Entry> list_dir(const fs::path& p) {
    std::vector<Entry> out;
    std::error_code ec;
    fs::directory_iterator it(
        p, fs::directory_options::skip_permission_denied, ec);
    for(; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::string name = it->path().filename().string();
        if(!g_hidden && name.starts_with('.')) continue;
        std::error_code e2;
        const bool link = it->is_symlink(e2);
        out.push_back({std::move(name), it->is_directory(e2), link});
    }
    std::ranges::sort(out, [](const Entry& a, const Entry& b) {
        if(a.dir != b.dir) return a.dir;
        return lower(a.name) < lower(b.name);
    });
    return out;
}

void reload(Node& n) {
    std::map<std::string, std::unique_ptr<Node>> old;
    for(auto& k : n.kids) old[k->name] = std::move(k);
    n.kids.clear();
    for(const Entry& e : list_dir(n.path)) {
        std::unique_ptr<Node> k;
        if(const auto it = old.find(e.name);
           it != old.end() && it->second->dir == e.dir) {
            k = std::move(it->second);
        } else {
            k = std::make_unique<Node>();
            k->name = e.name;
            k->path = n.path / e.name;
            k->dir = e.dir;
            k->link = e.link;
        }
        k->parent = &n;
        n.kids.push_back(std::move(k));
    }
    n.loaded = true;
    for(auto& k : n.kids) {
        if(!k->dir) continue;
        if(k->open) reload(*k);
        else k->loaded = false;
    }
}

void flatten(Node* n, int depth, const std::string& prefix, bool last) {
    const bool conn = n->dir || last;
    g_rows.push_back(
        {n,
         depth,
         depth ? prefix + (conn ? (last ? "└─ " : "├─ ") : "│  ") : ""});
    if(!n->dir || !n->open) return;
    if(!n->loaded) reload(*n);
    const std::string next = depth ? prefix + (last ? "   " : "│  ") : "";
    for(size_t i = 0; i < n->kids.size(); ++i)
        flatten(n->kids[i].get(), depth + 1, next, i + 1 == n->kids.size());
}

void sync_watches();

void rebuild(const fs::path& keep) {
    g_rows.clear();
    flatten(g_root.get(), 0, "", true);
    g_sel = 0;
    for(size_t i = 0; i < g_rows.size(); ++i)
        if(g_rows[i].n->path == keep) {
            g_sel = i;
            break;
        }
    sync_watches();
}

Node* cur() {
    return g_rows.empty() ? nullptr
                          : g_rows[std::min(g_sel, g_rows.size() - 1)].n;
}

fs::path cur_path() {
    Node* n = cur();
    return n ? n->path : fs::path{};
}

void set_root(const fs::path& p) {
    g_root = std::make_unique<Node>();
    g_root->path = p;
    g_root->name = p.filename().string();
    if(g_root->name.empty()) g_root->name = p.string();
    g_root->dir = g_root->open = true;
    g_top = 0;
    rebuild(p);
}

void refresh() {
    const fs::path keep = cur_path();
    reload(*g_root);
    rebuild(keep);
}

void reveal(const std::string& file) {
    std::error_code ec;
    const fs::path abs = fs::weakly_canonical(fs::absolute(file, ec), ec);
    if(ec) return;
    const fs::path rel = abs.lexically_relative(g_root->path);
    if(rel.empty() || *rel.begin() == "..") return;
    Node* n = g_root.get();
    for(const fs::path& part : rel) {
        if(!n->dir) break;
        if(!n->loaded) reload(*n);
        n->open = true;
        const std::string name = part.string();
        const auto it = std::ranges::find_if(
            n->kids, [&](const auto& k) { return k->name == name; });
        if(it == n->kids.end()) break;
        n = it->get();
    }
    rebuild(n->path);
}

void follow_init() {
    static const int id =
        events_on("buf_enter", [](const std::vector<std::string>& a) {
            if(!opts.tree_follow || !g_shown || !g_root || a.empty() ||
               a[0].empty())
                return;
            if(E.buf().nofile)
                return;  // [semstat] etc. would otherwise jump to the root row
            reveal(a[0]);
        });
    (void)id;
}

void open_file(const fs::path& p) {
    const fs::path rel = p.lexically_relative(fs::current_path());
    const bool inside = !rel.empty() && *rel.begin() != "..";
    buffer_open(inside ? rel.string() : p.string());
    focus(false);
}

void move(long d) {
    if(g_rows.empty()) return;
    const long n = static_cast<long>(g_rows.size());
    g_sel = static_cast<size_t>(
        std::clamp<long>(static_cast<long>(g_sel) + d, 0, n - 1));
}

void activate(Node* n) {
    if(!n->dir) return open_file(n->path);
    if(n == g_root.get()) return;
    n->open = !n->open;
    rebuild(n->path);
}

void expand_or_enter(Node* n) {
    if(!n->dir) return open_file(n->path);
    if(!n->open) {
        n->open = true;
        rebuild(n->path);
    } else if(g_sel + 1 < g_rows.size() &&
              g_rows[g_sel + 1].depth > g_rows[g_sel].depth) {
        ++g_sel;
    }
}

void collapse_or_parent(Node* n) {
    if(n->dir && n->open && n != g_root.get()) {
        n->open = false;
        rebuild(n->path);
    } else if(n->parent) {
        rebuild(n->parent->path);
    }
}

void palette_open(const fs::path& sel);
void say(const std::string& s) { editor_set_status_message("{}", s); }

bool within(const fs::path& p, const fs::path& base) {
    const fs::path rel = p.lexically_normal().lexically_relative(base);
    return !rel.empty() && *rel.begin() != "..";
}

fs::path target_dir(const fs::path& sel) {
    std::error_code ec;
    return fs::is_directory(sel, ec) ? sel : sel.parent_path();
}

std::vector<size_t> buffers_under(const fs::path& p) {
    std::vector<size_t> out;
    for(size_t i = 0; i < E.buffers.size(); ++i) {
        const Buffer& b = *E.buffers[i];
        if(b.filename.empty() || b.nofile) continue;
        std::error_code ec;
        const fs::path abs =
            fs::weakly_canonical(fs::absolute(b.filename, ec), ec);
        if(!ec && within(abs, p)) out.push_back(i);
    }
    return out;
}

void settle(const fs::path& focus) {
    reload(*g_root);
    reveal(focus.string());
}

void ask(std::string prompt, std::string initial,
         std::function<void(std::string)> done) {
    InputSpec in;
    in.prompt = std::move(prompt);
    in.initial = std::move(initial);
    in.w = 50;
    in.on_submit = std::move(done);
    input_open(std::move(in));
}

std::optional<fs::path> resolve(const fs::path& dir, std::string text,
                                bool& want_dir) {
    want_dir = !text.empty() && text.back() == '/';
    while(!text.empty() && text.back() == '/') text.pop_back();
    if(text.empty()) return std::nullopt;
    if(fs::path(text).is_absolute()) {
        say("Use a path relative to the target directory");
        return std::nullopt;
    }
    fs::path p = (dir / text).lexically_normal();
    if(!within(p, g_root->path)) {
        say("Path would leave the tree root");
        return std::nullopt;
    }
    return p;
}

struct Clip {
    fs::path src;
    bool cut = false;
};

Clip g_clip;

void op_add(const fs::path& sel) {
    const fs::path dir = target_dir(sel);
    const fs::path rel = dir.lexically_relative(g_root->path);
    const std::string where = rel == "." ? "./" : rel.string() + "/";
    ask("New in " + where + ": ", "", [dir](std::string text) {
        bool want_dir = false;
        const auto p = resolve(dir, std::move(text), want_dir);
        if(!p) return;
        std::error_code ec;
        if(fs::exists(*p, ec))
            return say(p->filename().string() + " already exists");
        if(want_dir) {
            fs::create_directories(*p, ec);
        } else {
            fs::create_directories(p->parent_path(), ec);
            if(!ec) {
                std::ofstream f(*p);
                if(!f) ec = std::make_error_code(std::errc::io_error);
            }
        }
        if(ec) return say("create failed: " + ec.message());
        settle(*p);
    });
}

void op_rename(const fs::path& sel) {
    if(sel == g_root->path) return say("Can't rename the tree root");
    if(!buffers_under(sel).empty())
        return say("Close the open buffer(s) for this path first");
    const fs::path dir = sel.parent_path();
    ask("Rename: ", sel.filename().string(), [sel, dir](std::string text) {
        bool want_dir = false;
        const auto p = resolve(dir, std::move(text), want_dir);
        if(!p || *p == sel) return;
        std::error_code ec;
        if(fs::exists(*p, ec))
            return say(p->filename().string() + " already exists");
        fs::create_directories(p->parent_path(), ec);
        if(!ec) fs::rename(sel, *p, ec);
        if(ec) return say("rename failed: " + ec.message());
        settle(*p);
    });
}

void op_delete(const fs::path& sel) {
    if(sel == g_root->path) return say("Refusing to delete the tree root");
    for(const size_t i : buffers_under(sel))
        if(E.buffers[i]->pt.modified())
            return say("Unsaved changes in an open buffer under this path");
    ask("Delete " + sel.filename().string() + "? [y/N] ",
        "",
        [sel](std::string t) {
            if(t.empty() || (t[0] != 'y' && t[0] != 'Y')) return;
            const auto bufs = buffers_under(sel);
            for(auto it = bufs.rbegin(); it != bufs.rend(); ++it)
                if(!buffer_close(*it, false)) return;
            std::error_code ec;
            fs::remove_all(sel, ec);
            if(ec) say("delete failed: " + ec.message());
            settle(sel);
        });
}

void stash(const fs::path& sel, bool cut) {
    if(sel == g_root->path) return say("Can't copy or cut the tree root");
    g_clip = {sel, cut};
    say(std::string(cut ? "Cut " : "Copied ") + sel.filename().string());
}
void op_copy(const fs::path& sel) { stash(sel, false); }
void op_cut(const fs::path& sel) { stash(sel, true); }

// Drops `src` into `dir` as `name`; if that name is taken, asks for another.
void paste_into(const fs::path& dir, const fs::path& src,
                const std::string& name, bool cut) {
    std::error_code ec;
    if(fs::is_directory(src, ec) && within(dir, src))
        return say("Can't paste a directory into itself");
    if(cut && !buffers_under(src).empty())
        return say("Close the open buffer(s) for this path first");
    const fs::path dst = dir / name;
    if(fs::exists(dst, ec)) {
        ask("Exists, new name: ", name, [dir, src, cut](std::string t) {
            if(t.empty() || t.find('/') != std::string::npos)
                return say("Give a plain name (no slashes)");
            paste_into(dir, src, t, cut);
        });
        return;
    }
    if(cut) {
        fs::rename(src, dst, ec);
        if(ec == std::errc::cross_device_link) {
            ec.clear();
            fs::copy(
                src,
                dst,
                fs::copy_options::recursive | fs::copy_options::copy_symlinks,
                ec);
            if(!ec) fs::remove_all(src, ec);
        }
    } else {
        fs::copy(src,
                 dst,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks,
                 ec);
    }
    if(ec) return say("paste failed: " + ec.message());
    if(cut) g_clip = {};
    settle(dst);
}

void op_paste(const fs::path& sel) {
    std::error_code ec;
    if(g_clip.src.empty()) return say("Clipboard is empty");
    if(!fs::exists(g_clip.src, ec))
        return say("Copied source no longer exists");
    paste_into(target_dir(sel),
               g_clip.src,
               g_clip.src.filename().string(),
               g_clip.cut);
}

constexpr size_t kNone = static_cast<size_t>(-1);
constexpr size_t kMaxIndex = 100000;  // safety cap on scanned entries

std::string g_query;  // active query; empty = no search
std::vector<std::string>
    g_index;  // every entry under the root, as "a/b/c", in tree order
std::vector<size_t> g_hits;  // ascending indices into g_index matching g_query
std::unordered_set<std::string> g_hit_set;  // same hits as rel paths

// dirs we list but never descend into (a fallback for roots outside a git repo)
bool skip_descent(std::string_view name) { return name == "node_modules"; }

// Gitignore support: ask git instead of re-implementing ignore rules (nested
// .gitignore files, negations, global excludes, .git/info/exclude ...).
struct GitAllow {
    bool active = false;  // false = not a repo / no git: don't filter
    std::unordered_set<std::string>
        paths;  // allowed files + all their ancestor dirs
};
GitAllow g_git;

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for(const char c : s) {
        if(c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

// Every file that is tracked, or untracked but not ignored, relative to the
// tree root; each file's parent directories are added too so the walk can
// still reach it.
void load_git_allow() {
    g_git = {};
    const std::string cmd =
        "git -C " + shell_quote(g_root->path.string()) +
        " ls-files -z --cached --others --exclude-standard 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if(!f) return;
    std::string out;
    char buf[65536];
    for(size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    if(pclose(f) != 0) return;  // not a repo, or git missing
    g_git.active = true;
    for(size_t start = 0; start < out.size();) {
        size_t end = out.find('\0', start);
        if(end == std::string::npos) end = out.size();
        std::string path = out.substr(start, end - start);
        start = end + 1;
        if(path.empty()) continue;
        for(size_t sl = path.find('/'); sl != std::string::npos;
            sl = path.find('/', sl + 1))
            g_git.paths.insert(path.substr(0, sl));
        g_git.paths.insert(std::move(path));
    }
}

void index_dir(const fs::path& abs, const std::string& rel) {
    for(const Entry& e : list_dir(abs)) {  // same sort order as the tree
        if(g_index.size() >= kMaxIndex) return;
        if(e.name.starts_with('.'))
            continue;  // hidden: never searched, even with H on
        const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
        if(g_git.active && !g_git.paths.contains(r)) continue;  // gitignored
        g_index.push_back(r);
        std::error_code ec;
        if(e.dir && !skip_descent(e.name) && !fs::is_symlink(abs / e.name, ec))
            index_dir(abs / e.name, r);
    }
}

// smartcase substring match. No '/' in the query -> match the file name only;
// a '/' in the query -> match the whole path relative to the root.
bool match(std::string_view rel, const std::string& q) {
    if(q.find('/') == std::string::npos) {
        const size_t s = rel.rfind('/');
        if(s != std::string_view::npos) rel.remove_prefix(s + 1);
    }
    if(std::ranges::any_of(
           q, [](unsigned char c) { return std::isupper(c) != 0; }))
        return rel.find(q) != std::string_view::npos;
    return !std::ranges::search(rel, q, [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            }).empty();
}

std::string ext_of(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? ""
                                                : lower(name.substr(dot + 1));
}

Style name_style(const Node& n) {
    if(&n == g_root.get())
        return theme_ui("ui.tree.root",
                        theme_ui("ui.tree.dir", Style{.attrs = ATTR_BOLD}));
    Style s;
    if(n.dir) {
        s = theme_ui("ui.tree.dir", Style{.attrs = ATTR_BOLD});
        if(n.open)
            if(const auto o = theme_override("ui.tree.open"))
                style_patch(s, *o);
    } else {
        const std::string ext = ext_of(n.name);
        std::optional<Style> o = theme_override("ui.tree.name." + n.name);
        if(!o && !ext.empty()) o = theme_override("ui.tree.ext." + ext);
        s = o ? *o
            : n.name.starts_with('.')
                ? theme_ui("ui.tree.hidden", Style{.attrs = ATTR_DIM})
                : theme_ui("ui.tree.file", Style{});
    }
    if(n.link)
        if(const auto o = theme_override("ui.tree.symlink")) style_patch(s, *o);
    return s;
}

// the active buffer's absolute path, recomputed only when its filename changes
const fs::path& active_path() {
    static std::string last;
    static fs::path p;
    const Buffer& b = E.buf();
    if(b.filename != last) {
        last = b.filename;
        p.clear();
        if(!last.empty() && !b.nofile) {
            std::error_code ec;
            p = fs::weakly_canonical(fs::absolute(last, ec), ec);
            if(ec) p.clear();
        }
    }
    return p;
}

std::string rel_of(const Node& n) {
    return n.path.lexically_relative(g_root->path).generic_string();
}

void run_query() {
    g_hits.clear();
    g_hit_set.clear();
    if(g_query.empty()) return;
    for(size_t i = 0; i < g_index.size(); ++i)
        if(match(g_index[i], g_query)) {
            g_hits.push_back(i);
            g_hit_set.insert(g_index[i]);
        }
}

// position of `p` in g_index; kNone for the root row (it sorts before
// everything)
size_t index_pos(fs::path p) {
    while(p != g_root->path && within(p, g_root->path)) {
        const std::string rel =
            p.lexically_relative(g_root->path).generic_string();
        const auto it = std::ranges::find(g_index, rel);
        if(it != g_index.end())
            return static_cast<size_t>(it - g_index.begin());
        p = p.parent_path();
    }
    return kNone;
}

// Nearest hit from `pos` in `step` direction (+1/-1), wrapping around.
// inclusive lets a hit at `pos` itself count (forward only).
std::optional<size_t> pick_hit(size_t pos, int step, bool inclusive) {
    if(g_hits.empty()) return std::nullopt;
    if(step > 0) {
        const auto it = pos == kNone ? g_hits.begin()
                        : inclusive  ? std::ranges::lower_bound(g_hits, pos)
                                     : std::ranges::upper_bound(g_hits, pos);
        return it == g_hits.end() ? g_hits.front() : *it;
    }
    const auto it =
        pos == kNone ? g_hits.end() : std::ranges::lower_bound(g_hits, pos);
    return it == g_hits.begin() ? g_hits.back() : *std::prev(it);
}

// Opens every ancestor of the hit and puts the cursor on it.
void show_hit(size_t idx) {
    reveal((g_root->path / g_index[idx]).string());
    const auto k = std::ranges::lower_bound(g_hits, idx) - g_hits.begin();
    say("match " + std::to_string(k + 1) + "/" + std::to_string(g_hits.size()));
}

// Search auto-expands directories to reach hits. These two let us put the
// tree back exactly how it was when the user cancels / refines the query.
void collect_open(const Node& n, std::set<fs::path>& out) {
    for(const auto& k : n.kids)
        if(k->dir && k->open) {
            out.insert(k->path);
            collect_open(*k, out);
        }
}
void restore_open(Node& n, const std::set<fs::path>& keep) {
    for(auto& k : n.kids)
        if(k->dir) {
            k->open = keep.count(k->path) > 0;
            restore_open(*k, keep);
        }
}

void search_clear() {
    g_query.clear();
    g_hits.clear();
    g_hit_set.clear();
    g_index.clear();
    g_index.shrink_to_fit();
}

void op_search(const fs::path&) {
    const fs::path origin = cur_path();
    std::set<fs::path> opened;
    collect_open(*g_root, opened);

    g_index.clear();
    load_git_allow();
    index_dir(g_root->path,
              "");  // one scan per '/', then typing only filters memory
    g_git = {};
    if(g_index.size() >= kMaxIndex)
        say("Search capped at " + std::to_string(kMaxIndex) + " entries");
    const size_t origin_pos =
        origin == g_root->path ? kNone : index_pos(origin);

    InputSpec in;
    in.prompt = "/";
    in.w = 50;
    in.on_change = [=](std::string_view q) {
        g_query = std::string(q);
        run_query();
        restore_open(*g_root,
                     opened);  // undo the previous keystroke's expansions
        const auto hit = pick_hit(origin_pos, +1, true);
        if(hit) show_hit(*hit);
        else rebuild(origin);
    };
    in.on_cancel = [=] {
        search_clear();
        restore_open(*g_root, opened);
        rebuild(origin);
    };
    in.on_submit = [](std::string) {
        if(g_query.empty()) return search_clear();
        if(g_hits.empty()) say("No match for \"" + g_query + "\"");
    };
    input_open(std::move(in));
}

void step_hit(int step) {
    if(g_hits.empty()) return say("No active search (press / first)");
    if(const auto hit = pick_hit(index_pos(cur_path()), step, false))
        show_hit(*hit);
}
void op_next(const fs::path&) { step_hit(+1); }
void op_prev(const fs::path&) { step_hit(-1); }

struct Action {
    int key;  // 0 = help-only row (the real handler is in tree_key)
    std::string_view keys;  // how the palette shows it
    std::string_view desc;
    void (*run)(const fs::path& sel);
};

const Action kActions[] = {
    {'a', "a", "Add file  (end with / for a directory)", op_add},
    {'r', "r", "Rename / move", op_rename},
    {'d', "d", "Delete (asks first)", op_delete},
    {'c', "c", "Copy", op_copy},
    {'x', "x", "Cut", op_cut},
    {'p', "p", "Paste into dir under cursor", op_paste},
    {'/', "/", "Search from root (recursive)", op_search},
    {'n', "n", "Next match", op_next},
    {'N', "N", "Previous match", op_prev},
    {'?', "?", "This palette", palette_open},
    {0, "j k", "Move down / up", nullptr},
    {0, "h l", "Collapse / expand", nullptr},
    {0, "o Enter", "Open file / toggle dir", nullptr},
    {0, ". Bksp", "Root into dir / up a level", nullptr},
    {0, "H R", "Toggle hidden / refresh", nullptr},
};
constexpr size_t kNActions = std::size(kActions);

bool run_action(int c) {
    for(const Action& a : kActions)
        if(a.key && a.key == c && a.run) {
            a.run(cur_path());
            return true;
        }
    return false;
}

struct Palette {
    OverlayId id = 0;
    size_t sel = 0;
    int kw = 0;     // widest key label
    int inner = 0;  // overlay width minus the border
};

void palette_open(const fs::path&) {
    auto st = std::make_shared<Palette>();
    int dw = 0;
    for(const Action& a : kActions) {
        st->kw = std::max(st->kw, text_width(a.keys));
        dw = std::max(dw, text_width(a.desc));
    }
    st->inner = st->kw + dw + 4;  // " " keys "  " desc " "

    OverlaySpec o;
    o.at = OverlaySpec::At::Center;
    o.w = st->inner + 2;
    o.h = static_cast<int>(kNActions) + 2;
    o.z =
        90;  // under input boxes (100) so a chosen action's prompt lands on top
    o.modal = true;
    o.framed = true;
    o.pass.clear();

    o.on_key = [st](int c) {
        const auto close = [&] { overlay_close(st->id); };
        switch(c) {
            case 'j':
            case ARROW_DOWN: st->sel = (st->sel + 1) % kNActions; return true;
            case 'k':
            case ARROW_UP:
                st->sel = (st->sel + kNActions - 1) % kNActions;
                return true;
            case '\x1b': return false;  // overlay layer closes it (esc_closes)
            case 'q':
            case '?': close(); return true;
            case '\r': {
                const Action a = kActions[st->sel];
                close();  // first: run() may open a prompt on top
                if(a.run) a.run(cur_path());
                return true;
            }
            default:
                for(const Action& a : kActions)
                    if(a.key && a.key == c && a.run) {
                        close();
                        a.run(cur_path());
                        break;
                    }
                return true;  // modal: swallow everything else
        }
    };

    o.on_paint = [st](OverlayId id) {
        overlay_clear(id);
        for(size_t i = 0; i < kNActions; ++i) {
            const Action& a = kActions[i];
            const int y = 1 + static_cast<int>(i);
            Style row = theme_ui("ui.menu", Style{});
            if(i == st->sel)
                row = theme_ui("ui.menu.select", Style{.attrs = ATTR_REVERSE});
            else if(!a.run)
                row = theme_ui("ui.menu.disabled", Style{.attrs = ATTR_DIM});
            Style key = row;
            style_patch(key,
                        theme_ui("ui.menu.key", Style{.attrs = ATTR_BOLD}));
            overlay_put(id,
                        1,
                        y,
                        std::string(static_cast<size_t>(st->inner), ' '),
                        row);
            overlay_put(id, 2, y, a.keys, key);
            overlay_put(id, 2 + st->kw + 2, y, a.desc, row);
        }
    };

    st->id = overlay_open(std::move(o));
}

}  // namespace

bool tree_visible() { return g_shown; }
bool tree_focused() { return focused(); }

void tree_move(long d) { move(d); }

void tree_goto(long row) {
    if(g_rows.empty()) return;
    g_sel = static_cast<size_t>(
        std::clamp<long>(row, 0, static_cast<long>(g_rows.size()) - 1));
}

void tree_layout(int total_cols) {
    g_cols = (g_shown && total_cols >= 50)
                 ? std::min(opts.tree_width, total_cols / 3) + 1
                 : 0;
}

int tree_cols() { return g_cols; }

void tree_toggle() {
    if(g_shown && focused()) {
        g_shown = false;
        focus(false);
        return;
    }
    if(!g_root) {
        g_hidden = opts.tree_hidden;
        std::error_code ec;
        set_root(fs::weakly_canonical(fs::current_path(ec), ec));
    }
    follow_init();
    g_shown = true;
    refresh();  // catch up on anything that changed while hidden
    load_git_status();
    focus(true);
    const Buffer& b = E.buf();
    if(!b.filename.empty() && !b.nofile) reveal(b.filename);
}

bool tree_key(int c) {
    if(!focused()) return false;
    if(g_cols == 0) {
        focus(false);
        return false;
    }
    Node* n = cur();
    switch(c) {
        case ARROW_DOWN: move(+1); break;
        case ARROW_UP: move(-1); break;
        case PAGE_DOWN: move(+10); break;
        case PAGE_UP: move(-10); break;
        case HOME: g_sel = 0; break;
        case END: g_sel = g_rows.empty() ? 0 : g_rows.size() - 1; break;
        case 'l':
        case ARROW_RIGHT:
            if(n) expand_or_enter(n);
            break;
        case '\r':
        case 'o':
            if(n) activate(n);
            break;
        case 'h':
        case ARROW_LEFT:
            if(n) collapse_or_parent(n);
            break;
        case 'H':
            g_hidden = !g_hidden;
            refresh();
            break;
        case 'R': refresh(); break;
        case '.':
            if(n && n->dir && n != g_root.get()) set_root(n->path);
            break;
        case BACKSPACE:
            if(g_root->path.has_parent_path() &&
               g_root->path.parent_path() != g_root->path) {
                const fs::path old = g_root->path;
                set_root(old.parent_path());
                rebuild(old);
            }
            break;
        case 'q':
            g_shown = false;
            focus(false);
            break;
        case '\x1b':
            if(!g_query.empty()) search_clear();
            else focus(false);
            break;
        case ':':
            focus(false);
            return false;  // now in NORMAL, so normal_feed opens the command
                           // line
        default:
            return run_action(c);  // file ops; everything else -> FILES keymap
    }
    return true;
}

struct GitMark {
    char staged = 0, work = 0;  // index / worktree column: M A D U ? or 0
};
std::unordered_map<std::string, GitMark>
    g_gstat;  // rel path -> marks (dirs inherit the strongest below them)
uint64_t g_gstat_gen = 0;
JobId g_gstat_job = 0;

char git_one(char c) {  // one porcelain column -> M A D, or 0 for none
    switch(c) {
        case 'M':
        case 'R':
        case 'C':
        case 'T': return 'M';
        case 'A': return 'A';
        case 'D': return 'D';
        default: return 0;
    }
}
int git_rank(char c) {
    switch(c) {
        case 'U': return 5;
        case 'D': return 4;
        case 'M': return 3;
        case 'A': return 2;
        case '?': return 1;
        default: return 0;
    }
}

void git_mark(std::string path, GitMark m) {  // file and every ancestor dir
    for(;;) {
        GitMark& cur = g_gstat[path];
        if(git_rank(m.staged) > git_rank(cur.staged)) cur.staged = m.staged;
        if(git_rank(m.work) > git_rank(cur.work)) cur.work = m.work;
        const size_t sl = path.rfind('/');
        if(sl == std::string::npos) break;
        path.resize(sl);
    }
}

void git_parse(std::string_view out) {
    g_gstat.clear();
    const size_t nl = out.find('\n');  // line 1: rev-parse --show-prefix
    if(nl == std::string_view::npos) return;
    const std::string prefix(out.substr(0, nl));  // "" or "sub/dir/"
    out.remove_prefix(nl + 1);
    while(out.size() >= 4) {
        const char x = out[0], y = out[1];
        size_t end = out.find('\0');
        if(end == std::string_view::npos) end = out.size();
        std::string path(out.substr(3, end - 3));
        out.remove_prefix(std::min(end + 1, out.size()));
        if(x == 'R' || x == 'C') {  // -z: the old name is the next field
            const size_t e2 = out.find('\0');
            out.remove_prefix(e2 == std::string_view::npos ? out.size()
                                                           : e2 + 1);
        }
        if(!path.starts_with(prefix)) continue;  // outside the tree root
        path.erase(0, prefix.size());
        if(!path.empty() && path.back() == '/')
            path.pop_back();  // untracked dir
        if(path.empty()) continue;
        GitMark m;
        if(x == '?') m.work = '?';
        else if(x == 'U' || y == 'U' || (x == 'A' && y == 'A') ||
                (x == 'D' && y == 'D'))
            m.work = 'U';
        else {
            m.staged = git_one(x);
            m.work = git_one(y);
        }
        git_mark(std::move(path), m);
    }
}

void load_git_status() {
    const uint64_t gen =
        ++g_gstat_gen;  // a newer request orphans older replies
    if(g_gstat_job) job_kill(g_gstat_job);
    g_gstat_job = 0;
    if(!opts.tree_git || !g_root) {
        g_gstat.clear();
        return;
    }
    const std::string r = shell_quote(g_root->path.string());
    g_gstat_job = job_start({
        .cmd = "git -C " + r +
               " rev-parse --show-prefix 2>/dev/null && git --no-optional-locks -C " + r +
               " status --porcelain=v1 -z --untracked-files=normal 2>/dev/null",
        .on_exit =
            [gen](JobResult res) {
                if(gen != g_gstat_gen) return;
                g_gstat_job = 0;
                if(res.code != 0)
                    g_gstat.clear();  // not a repo, or git missing
                else git_parse(res.out);
            },
    });
}

// live fs reactivity: one inotify watch per *open* dir (not recursive)
namespace {
std::map<fs::path, WatchId> g_fsw;
fs::path g_gitw_dir;
WatchId g_gitw = 0;
TimerId g_fs_timer = 0;
bool g_fs_struct = false;

void fs_event(const std::string& name, unsigned kind) {
    if(!g_shown || !g_root) return;
    if(name.ends_with(".winter.tmp")) return;  // our own atomic-save scratch
    g_fs_struct |= (kind & WATCH_STRUCT) != 0;
    if(g_fs_timer) return;  // throttle: at most one refresh per 120ms
    g_fs_timer = loop_after(120, [] {
        g_fs_timer = 0;
        if(!g_root) return;
        if(std::exchange(g_fs_struct, false)) refresh();
        load_git_status();
    });
}

void sync_watches() {
    std::set<fs::path> want;
    for(const Row& r : g_rows) {
        if(!r.n->dir || !r.n->open) continue;
        // .git churns on every `git status`; never watch inside it
        if(std::ranges::any_of(r.n->path,
                               [](const fs::path& c) { return c == ".git"; }))
            continue;
        want.insert(r.n->path);
    }
    for(auto it = g_fsw.begin(); it != g_fsw.end();) {
        if(want.contains(it->first)) {
            ++it;
            continue;
        }
        watch_remove(it->second);
        it = g_fsw.erase(it);
    }
    for(const fs::path& p : want)
        if(!g_fsw.contains(p))
            if(const WatchId id = watch_dir(p.string(), fs_event))
                g_fsw[p] = id;

    // stage/commit/checkout from another terminal only touches .git/index|HEAD
    const fs::path gd = g_root ? g_root->path / ".git" : fs::path{};
    if(gd != g_gitw_dir) {
        if(g_gitw) watch_remove(g_gitw);
        g_gitw = gd.empty() ? 0
                            : watch_dir(gd.string(),
                                        [](const std::string& n, unsigned) {
                                            if(n == "index" || n == "HEAD")
                                                fs_event("", 0);
                                        });
        g_gitw_dir = gd;
    }
}
}  // namespace

struct GitLook {
    const char* group;
    uint32_t fg;
};
GitLook git_look(char c) {
    switch(c) {
        case 'U': return {"ui.tree.git.conflict", 0xfab387};
        case 'D': return {"ui.tree.git.deleted", 0xf38ba8};
        case 'A': return {"ui.tree.git.added", 0xa6e3a1};
        case '?': return {"ui.tree.git.untracked", 0x94e2d5};
        default: return {"ui.tree.git.modified", 0xf9e2af};
    }
}

// staged marks share one group; worktree marks use the per-kind groups
Style git_style(char c, bool staged, bool dir) {
    const GitLook k = git_look(c);
    Style s = staged ? theme_ui("ui.tree.git.staged", Style{.fg = 0xa6e3a1})
                     : theme_ui(k.group, Style{.fg = k.fg});
    if(dir) s.attrs |= ATTR_DIM;  // dirs only inherit the mark
    return s;
}
const GitMark* git_mark_of(const Node& n) {
    if(g_gstat.empty() || &n == g_root.get()) return nullptr;
    const auto it = g_gstat.find(rel_of(n));
    return it == g_gstat.end() ? nullptr : &it->second;
}

const std::vector<TreeLine>& tree_view(int rows) {
    g_lines.clear();
    if(rows < 1) return g_lines;
    const size_t vis = static_cast<size_t>(rows);
    // scroll off
    {
        const long R = rows, total = static_cast<long>(g_rows.size());
        const long so = std::min<long>(opts.scrolloff, (R - 1) / 2);
        const long bot = std::min<long>(opts.scrolloff, R - 1 - so);
        const long pad = opts.scrolleof ? bot : 0;
        const long sel = static_cast<long>(g_sel);
        long off = static_cast<long>(g_top);
        if(sel < off + so) off = sel - so;
        if(sel > off + R - 1 - bot) off = sel - (R - 1 - bot);
        g_top = static_cast<size_t>(
            std::clamp(off, 0L, std::max(0L, total + pad - R)));
    }
    for(size_t y = 0; y < vis; ++y) {
        const size_t i = g_top + y;
        if(i >= g_rows.size()) {
            g_lines.emplace_back();
            continue;
        }
        const Row& r = g_rows[i];
        const Node& n = *r.n;
        const Icons& ic = icons();
        TreeLine l;
        l.selected = i == g_sel && focused();
        if(!r.guide.empty())
            l.segs.push_back(
                {r.guide, theme_ui("ui.tree.guide", Style{.attrs = ATTR_DIM})});

        Style ns = name_style(n);
        if(n.path == active_path())
            style_patch(ns,
                        theme_ui("ui.tree.active", Style{.attrs = ATTR_BOLD}));
        Style is = ns;  // icon looks like the name unless its own group is set
        style_patch(is,
                    theme_ui(n.dir ? "ui.tree.icon.dir" : "ui.tree.icon.file",
                             Style{}));
        if(!n.dir && opts.nerdicons)
            if(const auto c = file_icon_color(n.name)) is.fg = *c;
        if(!n.dir) {  // per-type icon colour: whole name, then extension
            std::optional<Style> o =
                theme_override("ui.tree.icon.name." + n.name);
            if(const std::string ext = ext_of(n.name); !o && !ext.empty())
                o = theme_override("ui.tree.icon.ext." + ext);
            if(o) style_patch(is, *o);
        }
        const GitMark* gm = git_mark_of(n);
        if(gm && opts.tree_git_names && !n.dir) {  // recolour the name itself
            const char c = gm->work ? gm->work : gm->staged;
            if(c) {
                style_patch(ns, Style{.fg = git_style(c, !gm->work, false).fg});
                if(c == '?') ns.attrs |= ATTR_ITALIC;
            }
        }
        if(!g_query.empty() && &n != g_root.get() &&
           g_hit_set.contains(rel_of(n)))
            style_patch(
                ns, theme_ui("ui.tree.match", Style{.attrs = ATTR_UNDERCURL}));

        std::string_view icon =
            n.dir ? (n.open ? ic.dir_open : ic.dir) : file_icon(n.name);
        if(opts.nerdicons)  // exactly one space after every nerd glyph
            while(!icon.empty() && icon.back() == ' ') icon.remove_suffix(1);
        if(!icon.empty())
            l.segs.push_back(
                {std::string(icon) + (opts.nerdicons ? " " : ""), is});
        l.segs.push_back(
            {n.dir && &n != g_root.get() ? n.name + '/' : n.name, ns});

        if(gm) {  // right-aligned "XY" like `git status -s`, only if it fits
            const int w = g_cols - 1;
            int used = 0;
            for(const TreeSeg& s : l.segs) used += text_width(s.text);
            if(used + 3 <= w) {
                l.segs.push_back(
                    {std::string(static_cast<size_t>(w - used - 2), ' '),
                     Style{}});
                l.segs.push_back(
                    {gm->staged ? std::string(1, gm->staged) : " ",
                     git_style(gm->staged ? gm->staged : 'M', true, n.dir)});
                l.segs.push_back(
                    {gm->work ? std::string(1, gm->work) : " ",
                     git_style(gm->work ? gm->work : 'M', false, n.dir)});
            }
        }
        g_lines.push_back(std::move(l));
    }
    return g_lines;
}

int tree_cursor_row() {
    return static_cast<int>(g_sel - std::min(g_sel, g_top));
}
