// Offline documentation, editor-independent half. See doc_core.hpp.
#include "doc_core.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "json.hpp"

namespace fs = std::filesystem;

namespace doc {
namespace {

bool is_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

std::string lower_s(std::string_view s) {
    std::string o(s);
    for(char& c : o)
        if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

bool read_file(const std::string& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if(!in) return false;
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

bool write_file(const fs::path& p, std::string_view data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if(ec) return false;
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    if(!o) return false;
    o.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(o);
}

// readers never see a half-written file
bool write_atomic(const fs::path& p, std::string_view data) {
    fs::path tmp = p;
    tmp += ".new";
    if(!write_file(tmp, data)) return false;
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if(ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string today() {
    return std::format("{:%Y-%m-%d}",
                       std::chrono::floor<std::chrono::seconds>(
                           std::chrono::system_clock::now()));
}

std::string with_commas(size_t n) {
    std::string s = std::to_string(n), o;
    for(size_t i = 0; i < s.size(); ++i) {
        if(i && (s.size() - i) % 3 == 0) o += ',';
        o += s[i];
    }
    return o;
}

}  // namespace

// ---- naming ---------------------------------------------------------------

bool valid_slug(std::string_view s) {
    if(s.empty() || s.size() > 80 || s.front() == '.') return false;
    for(const char c : s)
        if(!(is_ident(c) && c != '_') && c != '_' && c != '.' && c != '~' &&
           c != '-')
            return false;
    return true;
}

std::string slug_base(std::string_view slug) {
    return std::string(slug.substr(0, slug.find('~')));
}

namespace {

#define DEVDOCS(slug, name, fence, sep, fts)                                   \
    {slug, name, fence, sep, fts, Origin::DevDocs, {}, {}, {}, {}, {}, {}, {}},
#define MDTREE(                                                                \
    slug, name, fence, sep, fts, repo, ref, subdir, strip, from, to)           \
    {slug,                                                                     \
     name,                                                                     \
     fence,                                                                    \
     sep,                                                                      \
     fts,                                                                      \
     Origin::MdTree,                                                           \
     repo,                                                                     \
     ref,                                                                      \
     subdir,                                                                   \
     strip,                                                                    \
     from,                                                                     \
     to,                                                                       \
     {}},
#define JAVADOC(slug, name, fence, sep, fts, coords)                           \
    {slug,                                                                     \
     name,                                                                     \
     fence,                                                                    \
     sep,                                                                      \
     fts,                                                                      \
     Origin::Javadoc,                                                          \
     {},                                                                       \
     {},                                                                       \
     {},                                                                       \
     {},                                                                       \
     {},                                                                       \
     {},                                                                       \
     coords},
#define LANGONLY(slug, fence, sep, fts)                                        \
    {slug, "", fence, sep, fts, Origin::Unlisted, {}, {}, {}, {}, {}, {}, {}},
constexpr Docset kDocsets[] = {
#include "docsets.inc"
};
#undef DEVDOCS
#undef MDTREE
#undef JAVADOC
#undef LANGONLY

constexpr LangInfo kUnknown{"", "", "."};

// "!cpp c++" -> has("cpp") = 2 (own), has("c++") = 1, has("lua") = 0
int ft_rank(std::string_view fts, std::string_view ft) {
    size_t i = 0;
    while(i < fts.size()) {
        size_t e = fts.find(' ', i);
        if(e == std::string_view::npos) e = fts.size();
        std::string_view t = fts.substr(i, e - i);
        const bool own = t.starts_with('!');
        if(own) t.remove_prefix(1);
        if(!t.empty() && t == ft) return own ? 2 : 1;
        i = e + 1;
    }
    return 0;
}
}  // namespace

const LangInfo& lang_for(std::string_view slug) {
    static const std::vector<LangInfo> t = [] {
        std::vector<LangInfo> v;
        for(const Docset& d : kDocsets) v.push_back({d.slug, d.fence, d.sep});
        return v;
    }();
    const std::string b = slug_base(slug);
    for(const LangInfo& l : t)
        if(l.base == b) return l;
    return kUnknown;
}

const Docset* docset_for(std::string_view base) {
    for(const Docset& d : kDocsets)
        if(d.slug == base) return &d;
    return nullptr;
}

const Docset* source_for(std::string_view base) {
    const Docset* d = docset_for(base);
    return d && d->origin == Origin::MdTree ? d : nullptr;
}

const Docset* javadoc_for(std::string_view base) {
    const Docset* d = docset_for(base);
    return d && d->origin == Origin::Javadoc ? d : nullptr;
}

std::vector<const Docset*> all_docsets() {
    std::vector<const Docset*> v;
    for(const Docset& d : kDocsets)
        if(d.origin != Origin::Unlisted) v.push_back(&d);
    return v;
}

std::vector<std::string_view> bases_for_filetype(std::string_view ft) {
    std::vector<std::string_view> own, rest;
    for(const Docset& d : kDocsets) {
        const int r = ft_rank(d.fts, ft);
        if(r == 2) own.push_back(d.slug);
        else if(r == 1) rest.push_back(d.slug);
    }
    own.insert(own.end(), rest.begin(), rest.end());
    return own;
}

// ---- cursor token ---------------------------------------------------------

std::string token_at(std::string_view line, size_t col) {
    const size_t n = line.size();
    if(col >= n) return {};
    const auto allowed = [&](size_t i) {
        const char c = line[i];
        if(is_ident(c) || c == '.') return true;
        if(c == ':')
            return (i > 0 && line[i - 1] == ':') ||
                   (i + 1 < n && line[i + 1] == ':');
        if(c == '-') return i + 1 < n && line[i + 1] == '>';
        if(c == '>') return i > 0 && line[i - 1] == '-';
        return false;
    };
    if(!allowed(col)) return {};
    size_t start = col;
    while(start > 0 && allowed(start - 1)) --start;
    size_t end;
    if(is_ident(line[col])) {
        end = col;
        while(end < n && is_ident(line[end])) ++end;
    } else {  // on a separator: the segment to its left
        end = col;
        while(end > start && !is_ident(line[end - 1])) --end;
    }
    std::string raw(line.substr(start, end - start));
    while(!raw.empty() && !is_ident(raw.front())) raw.erase(raw.begin());
    return raw;
}

std::string normalize_name(std::string_view name, std::string_view sep) {
    std::string o;
    for(size_t i = 0; i < name.size(); ++i) {
        if(name.compare(i, 2, "->") == 0 || name.compare(i, 2, "::") == 0) {
            o += sep;
            ++i;
        } else if(name[i] == '.') {
            o += sep;
        } else {
            o += name[i];
        }
    }
    return o;
}

std::vector<std::string> scope_prefixes(std::string_view name,
                                        std::string_view sep) {
    std::vector<std::string> out;
    std::string cur(name);
    if(cur.empty()) return out;
    out.push_back(cur);
    while(true) {
        const size_t p = cur.rfind(sep);
        if(p == std::string::npos || p == 0) break;
        cur.resize(p);
        out.push_back(cur);
    }
    return out;
}

// ---- lookup ---------------------------------------------------------------

bool load_index(const std::string& tsv_path, Index& out) {
    std::string text;
    if(!read_file(tsv_path, text)) return false;
    out.entries.clear();
    size_t from = 0;
    while(from < text.size()) {
        size_t nl = text.find('\n', from);
        if(nl == std::string::npos) nl = text.size();
        const std::string_view line =
            std::string_view(text).substr(from, nl - from);
        from = nl + 1;
        const size_t a = line.find('\t');
        if(a == std::string_view::npos) continue;
        const size_t b = line.find('\t', a + 1);
        Entry e;
        e.name = std::string(line.substr(0, a));
        e.target = std::string(line.substr(
            a + 1,
            b == std::string_view::npos ? std::string_view::npos : b - a - 1));
        if(b != std::string_view::npos)
            e.type = std::string(line.substr(b + 1));
        if(e.name.empty() || e.target.empty()) continue;
        out.entries.push_back(std::move(e));
    }
    if(!std::is_sorted(
           out.entries.begin(),
           out.entries.end(),
           [](const Entry& x, const Entry& y) { return x.name < y.name; }))
        std::stable_sort(
            out.entries.begin(),
            out.entries.end(),
            [](const Entry& x, const Entry& y) { return x.name < y.name; });
    return true;
}

std::vector<const Entry*> find_exact(const Index& ix, std::string_view name) {
    std::vector<const Entry*> r;
    const auto lo = std::lower_bound(ix.entries.begin(),
                                     ix.entries.end(),
                                     name,
                                     [](const Entry& e, std::string_view n) {
                                         return std::string_view(e.name) < n;
                                     });
    for(auto it = lo; it != ix.entries.end() && it->name == name; ++it)
        r.push_back(&*it);
    if(!r.empty()) return r;
    const std::string want = lower_s(name);
    for(const Entry& e : ix.entries)
        if(e.name.size() == want.size() && lower_s(e.name) == want)
            r.push_back(&e);
    return r;
}

// ---- installed docsets ------------------------------------------------------

std::optional<DocsetInfo> read_meta(const std::string& dir) {
    std::string text;
    if(!read_file(dir + "/meta.json", text)) return std::nullopt;
    const auto j = Json::parse(text);
    if(!j || !j->is_object() || !(*j)["slug"].is_string()) return std::nullopt;
    DocsetInfo d;
    d.slug = std::string((*j)["slug"].as_str());
    d.name = std::string((*j)["name"].as_str());
    d.version = std::string((*j)["version"].as_str());
    d.installed = std::string((*j)["installed"].as_str());
    d.mtime = (*j)["mtime"].as_int();
    d.entries = static_cast<size_t>((*j)["entries"].as_int());
    d.pages = static_cast<size_t>((*j)["pages"].as_int());
    d.html = (*j)["html"].as_bool();
    return d;
}

std::vector<DocsetInfo> installed(const std::string& root) {
    std::vector<DocsetInfo> out;
    std::error_code ec;
    for(const auto& de : fs::directory_iterator(root, ec)) {
        const std::string name = de.path().filename().string();
        if(name.empty() || name.front() == '.' || !de.is_directory(ec))
            continue;
        if(auto m = read_meta(de.path().string()); m && m->slug == name)
            out.push_back(std::move(*m));
    }
    std::sort(
        out.begin(), out.end(), [](const DocsetInfo& a, const DocsetInfo& b) {
            return a.slug < b.slug;
        });
    return out;
}

// ---- recently viewed --------------------------------------------------------

namespace {
constexpr size_t kRecentMax = 40;

void clean_field(std::string& s) {
    for(char& c : s)
        if(c == '\t' || c == '\n' || c == '\r') c = ' ';
}
}  // namespace

std::vector<Recent> load_recent(const std::string& root) {
    std::vector<Recent> out;
    std::string text;
    if(!read_file(root + "/recent.state", text)) return out;
    size_t from = 0;
    while(from < text.size()) {
        size_t nl = text.find('\n', from);
        if(nl == std::string::npos) nl = text.size();
        const std::string_view line =
            std::string_view(text).substr(from, nl - from);
        from = nl + 1;
        const size_t a = line.find('\t');
        if(a == std::string_view::npos) continue;
        const size_t b = line.find('\t', a + 1);
        if(b == std::string_view::npos) continue;
        Recent r{std::string(line.substr(0, a)),
                 std::string(line.substr(a + 1, b - a - 1)),
                 std::string(line.substr(b + 1))};
        if(!r.slug.empty() && !r.target.empty()) out.push_back(std::move(r));
    }
    return out;
}

void note_recent(const std::string& root, const Recent& r) {
    std::vector<Recent> all = load_recent(root);
    std::erase_if(all, [&](const Recent& x) {
        return x.slug == r.slug && x.target == r.target;
    });
    Recent c = r;
    clean_field(c.slug);
    clean_field(c.name);
    clean_field(c.target);
    all.insert(all.begin(), std::move(c));
    if(all.size() > kRecentMax) all.resize(kRecentMax);
    std::string out;
    for(const Recent& x : all)
        out += x.slug + "\t" + x.name + "\t" + x.target + "\n";
    write_atomic(fs::path(root) / "recent.state", out);
}

std::string splice_recent(std::string_view index_md,
                          const std::vector<Recent>& recent) {
    if(recent.empty()) return std::string(index_md);
    std::string sec = "## Recently viewed\n\n";
    for(const Recent& r : recent) {
        std::string path = r.target, frag;
        if(const size_t h = path.find('#'); h != std::string::npos) {
            frag = path.substr(h + 1);
            path.resize(h);
        }
        std::string url = pct_encode(r.slug + "/" + path);
        if(!frag.empty()) url += "#" + pct_encode(frag);
        sec += "- [" + md_escape(r.name) + "](" + url + ") \xE2\x80\x94 " +
               r.slug + "\n";
    }
    sec += "\n";
    // after the title line (and its blank line)
    size_t at = 0;
    if(index_md.starts_with("#")) {
        const size_t nl = index_md.find('\n');
        at = nl == std::string_view::npos ? index_md.size() : nl + 1;
        while(at < index_md.size() && index_md[at] == '\n') ++at;
    }
    std::string o(index_md.substr(0, at));
    if(!o.empty() && !o.ends_with("\n\n")) o += "\n";
    o += sec;
    o += index_md.substr(at);
    return o;
}

// ---- build ------------------------------------------------------------------

namespace {

// -- db.json: one flat object of "page path": "html", read in place --

struct PageRef {
    std::string key, stem;  // db key, file stem (file_key)
    size_t vs = 0, ve = 0;  // raw JSON string body in the db text
};

std::string json_unescape(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    const auto hex4 = [&](size_t i, uint32_t& v) {
        v = 0;
        if(i + 4 > s.size()) return false;
        for(size_t k = 0; k < 4; ++k) {
            const char c = s[i + k];
            v <<= 4;
            if(c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if(c >= 'a' && c <= 'f')
                v |= static_cast<uint32_t>(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F')
                v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    };
    const auto utf8 = [&](uint32_t c) {
        if(c < 0x80) o += static_cast<char>(c);
        else if(c < 0x800) {
            o += static_cast<char>(0xC0 | (c >> 6));
            o += static_cast<char>(0x80 | (c & 0x3F));
        } else if(c < 0x10000) {
            o += static_cast<char>(0xE0 | (c >> 12));
            o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (c >> 18));
            o += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (c & 0x3F));
        }
    };
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] != '\\' || i + 1 >= s.size()) {
            o += s[i];
            continue;
        }
        const char e = s[++i];
        switch(e) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': o += '\r'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'u': {
                uint32_t v;
                if(!hex4(i + 1, v)) {
                    o += "\\u";
                    break;
                }
                i += 4;
                if(v >= 0xD800 && v < 0xDC00 && i + 6 < s.size() + 0 &&
                   s[i + 1] == '\\' && s[i + 2] == 'u') {
                    uint32_t lo;
                    if(hex4(i + 3, lo) && lo >= 0xDC00 && lo < 0xE000) {
                        v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                if(v >= 0xD800 && v < 0xE000) v = 0xFFFD;
                utf8(v);
                break;
            }
            default: o += e;  // \" \\ \/ and anything odd
        }
    }
    return o;
}

// end of the string body that starts at `i` (just after the opening quote)
size_t string_end(std::string_view t, size_t i) {
    while(i < t.size()) {
        if(t[i] == '\\') i += 2;
        else if(t[i] == '"') return i;
        else ++i;
    }
    return std::string_view::npos;
}

bool scan_db(
    std::string_view t,
    std::vector<std::pair<std::string, std::pair<size_t, size_t>>>& out,
    std::string& err) {
    size_t i = 0;
    const auto ws = [&] {
        while(i < t.size() &&
              (t[i] == ' ' || t[i] == '\n' || t[i] == '\t' || t[i] == '\r'))
            ++i;
    };
    ws();
    if(i >= t.size() || t[i] != '{') {
        err = "db.json is not an object";
        return false;
    }
    ++i;
    while(true) {
        ws();
        if(i >= t.size()) break;
        if(t[i] == '}') return true;
        if(t[i] == ',') {
            ++i;
            continue;
        }
        if(t[i] != '"') break;
        const size_t ke = string_end(t, i + 1);
        if(ke == std::string_view::npos) break;
        std::string key = json_unescape(t.substr(i + 1, ke - i - 1));
        i = ke + 1;
        ws();
        if(i >= t.size() || t[i] != ':') break;
        ++i;
        ws();
        if(i >= t.size()) break;
        if(t[i] == '"') {
            const size_t ve = string_end(t, i + 1);
            if(ve == std::string_view::npos) break;
            out.push_back({
                std::move(key), {i + 1, ve}
            });
            i = ve + 1;
        } else {  // null or anything else: no page
            while(i < t.size() && t[i] != ',' && t[i] != '}') ++i;
        }
    }
    err = "db.json is truncated or malformed";
    return false;
}

// -- grouping of the level 1 index --

struct GNode {
    std::string title;
    size_t count = 0;
    std::vector<const Entry*> own;
    std::vector<GNode> kids;
};

constexpr size_t kGroupAbove =
    60;  // a group larger than this is split by prefix
constexpr size_t kMaxDepth = 3;

std::vector<std::string_view> split_sep(std::string_view s,
                                        std::string_view sep) {
    std::vector<std::string_view> o;
    size_t from = 0;
    while(true) {
        const size_t p = s.find(sep, from);
        if(p == std::string_view::npos) {
            o.push_back(s.substr(from));
            return o;
        }
        o.push_back(s.substr(from, p - from));
        from = p + sep.size();
    }
}

bool entry_less(const Entry* a, const Entry* b) {
    const std::string la = lower_s(a->name), lb = lower_s(b->name);
    if(la != lb) return la < lb;
    if(a->name != b->name) return a->name < b->name;
    return a->target < b->target;
}

void build_groups(GNode& g,
                  std::vector<const Entry*> es,
                  size_t depth,
                  std::string_view sep,
                  bool qual) {
    g.count = es.size();
    if(es.size() <= kGroupAbove || depth >= kMaxDepth) {
        std::sort(es.begin(), es.end(), entry_less);
        g.own = std::move(es);
        return;
    }
    std::map<std::string, std::vector<const Entry*>, std::less<>> parts;
    std::vector<const Entry*> own;
    for(const Entry* e : es) {
        if(qual) {
            const auto segs = split_sep(e->name, sep);
            if(segs.size() > depth + 1) {
                std::string key;
                for(size_t i = 0; i <= depth; ++i) {
                    key += segs[i];
                    key += sep;
                }
                parts[key].push_back(e);
            } else {
                own.push_back(e);
            }
        } else {
            char c = e->name.empty() ? '#' : e->name[0];
            if(c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if(!((c >= 'A' && c <= 'Z'))) c = '#';
            parts[std::string(1, c)].push_back(e);
        }
    }
    std::sort(own.begin(), own.end(), entry_less);
    g.own = std::move(own);
    for(auto& [key, v] : parts) {
        GNode k;
        k.title = key;
        // letter groups are leaves; prefix groups may split again
        if(qual) build_groups(k, std::move(v), depth + 1, sep, qual);
        else {
            k.count = v.size();
            std::sort(v.begin(), v.end(), entry_less);
            k.own = std::move(v);
        }
        g.kids.push_back(std::move(k));
    }
}

std::string entry_url(const std::string& target, bool html) {
    std::string stem = target, frag;
    if(const size_t h = stem.find('#'); h != std::string::npos) {
        frag = stem.substr(h + 1);
        stem.resize(h);
    }
    if(html) {
        if(stem.ends_with(".md")) stem.resize(stem.size() - 3);
        stem = "html/" + stem + ".html";
    }
    std::string u = pct_encode(stem);
    if(!frag.empty()) u += "#" + pct_encode(frag);
    return u;
}

void render_md(const GNode& g, size_t level, std::string& o) {
    if(!g.title.empty()) {
        o += std::string(std::min<size_t>(level, 6), '#') + " " +
             md_escape(g.title) + " (" + std::to_string(g.count) + ")\n\n";
    }
    if(!g.own.empty()) {
        for(const Entry* e : g.own)
            o += "- [" + md_escape(e->name) + "](" +
                 entry_url(e->target, false) + ")\n";
        o += "\n";
    }
    for(const GNode& k : g.kids) render_md(k, level + 1, o);
}

void render_html(const GNode& g, std::string& o) {
    if(!g.title.empty())
        o += "<details><summary>" + html_escape_text(g.title) + " (" +
             std::to_string(g.count) + ")</summary>\n";
    if(!g.own.empty()) {
        o += "<ul>\n";
        for(const Entry* e : g.own)
            o += "<li><a href=\"" +
                 html_escape_text(entry_url(e->target, true)) + "\">" +
                 html_escape_text(e->name) + "</a></li>\n";
        o += "</ul>\n";
    }
    for(const GNode& k : g.kids) render_html(k, o);
    if(!g.title.empty()) o += "</details>\n";
}

constexpr const char* kHtmlHead =
    "<!doctype html>\n<html><head><meta charset=\"utf-8\"><meta "
    "name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>%TITLE%</title>\n"
    "<style>body{font-family:sans-serif;max-width:60em;margin:1em "
    "auto;padding:0 1em}"
    "summary{cursor:pointer;font-weight:bold;margin:.3em 0}ul{margin:.2em 0 "
    ".6em 1.2em;"
    "padding:0;list-style:none}details "
    "details{margin-left:1em}</style></head><body>\n";

// a substring filter; w3m ignores this, a browser uses it
constexpr const char* kHtmlFilter =
    "<input id=\"q\" type=\"search\" placeholder=\"filter\" autofocus>\n";
constexpr const char* kHtmlScript =
    "<script>\n"
    "var q=document.getElementById('q');\n"
    "q.oninput=function(){var "
    "v=q.value.toLowerCase(),li=document.querySelectorAll('li'),i;\n"
    "for(i=0;i<li.length;i++)li[i].hidden=!!v&&li[i].textContent.toLowerCase()."
    "indexOf(v)<0;\n"
    "var d=document.querySelectorAll('details');\n"
    "for(i=d.length-1;i>=0;i--){var "
    "n=d[i].querySelectorAll('li:not([hidden])').length;\n"
    "d[i].hidden=!!v&&!n;d[i].open=!!v&&n>0;}};\n"
    "</script>\n";

std::string html_head(std::string_view title) {
    std::string h = kHtmlHead;
    const size_t p = h.find("%TITLE%");
    h.replace(p, 7, html_escape_text(title));
    return h;
}

struct Topic {
    std::string feature;
    std::string target;
};

}  // namespace

bool build_docset(const BuildOpts& o,
                  const Progress& say,
                  BuildStats& st,
                  std::string& err) {
    const auto say_ = [&](const std::string& s) {
        if(say) say(s);
    };
    const fs::path out(o.out_dir);

    // 1. inputs
    say_("reading index.json");
    std::string idx_text, db_text;
    if(!read_file(o.dl_dir + "/index.json", idx_text) || idx_text.empty()) {
        err = "index.json is missing";
        return false;
    }
    std::string jerr;
    const auto idx = Json::parse(idx_text, &jerr);
    if(!idx || !idx->is_object() || !(*idx)["entries"].is_array()) {
        err = "index.json is not a DevDocs index" +
              (jerr.empty() ? "" : " (" + jerr + ")");
        return false;
    }
    say_("reading db.json");
    if(!read_file(o.dl_dir + "/db.json", db_text) || db_text.empty()) {
        err = "db.json is missing";
        return false;
    }
    std::vector<std::pair<std::string, std::pair<size_t, size_t>>> raw;
    if(!scan_db(db_text, raw, err)) return false;

    // pages we can write, and every key a link may point at
    std::vector<PageRef> pages;
    std::unordered_set<std::string> keys, stems;
    for(auto& [k, span] : raw) {
        std::string stem = file_key(k);
        if(stem.empty() || keys.contains(k) || stems.contains(stem)) {
            ++st.skipped_pages;
            continue;
        }
        keys.insert(k);
        stems.insert(stem);
        pages.push_back({k, std::move(stem), span.first, span.second});
    }
    raw.clear();
    raw.shrink_to_fit();
    if(pages.empty()) {
        err = "db.json holds no pages";
        return false;
    }
    std::sort(pages.begin(),
              pages.end(),
              [](const PageRef& a, const PageRef& b) { return a.key < b.key; });

    // 2./3. convert and write
    const LangInfo& li = lang_for(o.slug);
    ConvertCtx cx;
    cx.fence = std::string(li.fence);
    cx.pages = &keys;
    auto last = std::chrono::steady_clock::now();
    for(size_t i = 0; i < pages.size(); ++i) {
        const PageRef& p = pages[i];
        const std::string html =
            json_unescape(std::string_view(db_text).substr(p.vs, p.ve - p.vs));
        cx.page = p.key;
        const std::string md = o.markdown ? html : html_to_md(html, cx);
        if(!write_file(out / (p.stem + ".md"), md)) {
            ++st.skipped_pages;
            continue;
        }
        if(o.save_html && !o.markdown &&
           !write_file(out / "html" / (p.stem + ".html"),
                       html_to_page(html, p.key, cx))) {
            err = "can't write html/" + p.stem + ".html";
            return false;
        }
        ++st.pages;
        const auto now = std::chrono::steady_clock::now();
        if(now - last > std::chrono::milliseconds(400)) {
            last = now;
            say_("converting " + std::to_string(i + 1) + "/" +
                 std::to_string(pages.size()));
        }
    }
    db_text.clear();
    db_text.shrink_to_fit();
    if(st.pages == 0) {
        err = "no page could be written";
        return false;
    }

    // 4. index.tsv
    say_("indexing");
    std::unordered_map<std::string, std::string> stem_of;  // key -> stem
    for(const PageRef& p : pages) stem_of[p.key] = p.stem;
    std::vector<Entry> entries;
    for(const Json& e : (*idx)["entries"].items()) {
        std::string name(e["name"].as_str()), path(e["path"].as_str()),
            type(e["type"].as_str());
        std::string anchor;
        if(const size_t h = path.find('#'); h != std::string::npos) {
            anchor = path.substr(h + 1);
            path.resize(h);
        }
        clean_field(name);
        clean_field(type);
        clean_field(anchor);
        const auto it = stem_of.find(path);
        if(name.empty() || it == stem_of.end()) {
            ++st.skipped_entries;
            continue;
        }
        entries.push_back(
            {std::move(name),
             it->second + ".md" + (anchor.empty() ? "" : "#" + anchor),
             type.empty() ? "Other" : std::move(type)});
    }
    std::sort(
        entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if(a.name != b.name) return a.name < b.name;
            return a.target < b.target;
        });
    entries.erase(std::unique(entries.begin(),
                              entries.end(),
                              [](const Entry& a, const Entry& b) {
                                  return a.name == b.name &&
                                         a.target == b.target;
                              }),
                  entries.end());
    st.entries = entries.size();
    {
        std::string tsv;
        tsv.reserve(entries.size() * 64);
        for(const Entry& e : entries)
            tsv += e.name + "\t" + e.target + "\t" + e.type + "\n";
        if(!write_file(out / "index.tsv", tsv)) {
            err = "can't write index.tsv";
            return false;
        }
    }

    // 5. level 1: INDEX.md (+ INDEX.html)
    say_("writing index");
    std::vector<std::string> type_order;
    for(const Json& t : (*idx)["types"].items()) {
        const std::string n(t["name"].as_str());
        if(!n.empty() && std::find(type_order.begin(), type_order.end(), n) ==
                             type_order.end())
            type_order.push_back(n);
    }
    {
        std::set<std::string> seen(type_order.begin(), type_order.end()), extra;
        for(const Entry& e : entries)
            if(!seen.contains(e.type)) extra.insert(e.type);
        type_order.insert(type_order.end(), extra.begin(), extra.end());
    }
    std::map<std::string, std::vector<const Entry*>> by_type;
    for(const Entry& e : entries) by_type[e.type].push_back(&e);
    const std::string_view sep = li.sep;
    std::vector<GNode> groups;
    for(const std::string& t : type_order) {
        const auto it = by_type.find(t);
        if(it == by_type.end() || it->second.empty()) continue;
        size_t with_sep = 0;
        for(const Entry* e : it->second)
            if(e->name.find(sep) != std::string::npos) ++with_sep;
        const bool qual = with_sep * 2 > it->second.size();
        GNode g;
        g.title = t;
        build_groups(g, it->second, 0, sep, qual);
        groups.push_back(std::move(g));
    }

    // Features: a user's topics.tsv, else the docset's own landing page
    std::vector<Topic> topics;
    {
        std::string tt;
        if(read_file((out / "topics.tsv").string(), tt)) {
            std::unordered_map<std::string, const Entry*> first;
            for(const Entry& e : entries) first.emplace(e.name, &e);
            size_t from = 0;
            while(from < tt.size()) {
                size_t nl = tt.find('\n', from);
                if(nl == std::string::npos) nl = tt.size();
                std::string line = tt.substr(from, nl - from);
                from = nl + 1;
                const size_t tab = line.find('\t');
                if(line.empty() || line[0] == '#' || tab == std::string::npos)
                    continue;
                std::string feat = line.substr(0, tab),
                            ref = line.substr(tab + 1);
                while(!ref.empty() && (ref.back() == '\r' || ref.back() == ' '))
                    ref.pop_back();
                if(const auto it = first.find(ref); it != first.end())
                    topics.push_back({feat, it->second->target});
                else if(ref.find(".md") != std::string::npos)
                    topics.push_back({feat, ref});
            }
        }
        if(topics.empty()) {
            if(const auto it = stem_of.find("index"); it != stem_of.end())
                topics.push_back({"Overview", it->second + ".md"});
        }
    }

    const std::string title = o.name.empty() ? o.slug : o.name;
    const std::string today_s = today();
    {
        std::string md =
            "# " + md_escape(title) + "\n\n" + with_commas(entries.size()) +
            " entries" +
            (o.version.empty() ? "" : " \xC2\xB7 version " + o.version) +
            " \xC2\xB7 installed " + today_s + "\n\n";
        if(!topics.empty()) {
            md += "## Features\n\n";
            for(const Topic& t : topics)
                md += "- [" + md_escape(t.feature) + "](" +
                      entry_url(t.target, false) + ")\n";
            md += "\n";
        }
        for(const GNode& g : groups) render_md(g, 2, md);
        if(!write_file(out / "INDEX.md", md)) {
            err = "can't write INDEX.md";
            return false;
        }
    }
    if(o.save_html) {
        std::string h =
            html_head(title) + "<h1>" + html_escape_text(title) + "</h1>\n";
        h += "<p>" + with_commas(entries.size()) + " entries</p>\n";
        h += kHtmlFilter;
        if(!topics.empty()) {
            h += "<details open><summary>Features</summary>\n<ul>\n";
            for(const Topic& t : topics)
                h += "<li><a href=\"" +
                     html_escape_text(entry_url(t.target, true)) + "\">" +
                     html_escape_text(t.feature) + "</a></li>\n";
            h += "</ul></details>\n";
        }
        for(const GNode& g : groups) render_html(g, h);
        h += kHtmlScript;
        h += "</body></html>\n";
        if(!write_file(out / "INDEX.html", h)) {
            err = "can't write INDEX.html";
            return false;
        }
    }

    // the raw downloads are not part of the layout
    std::error_code ec;
    fs::remove_all(o.dl_dir, ec);

    // meta.json last: it marks the install as complete
    Json meta = Json::obj({
        {"slug",      o.slug                          },
        {"name",      title                           },
        {"version",   o.version                       },
        {"mtime",     static_cast<int64_t>(o.mtime)   },
        {"installed", today_s                         },
        {"source",    o.source                        },
        {"entries",   static_cast<int64_t>(st.entries)},
        {"pages",     static_cast<int64_t>(st.pages)  },
        {"html",      o.save_html                     }
    });
    if(!write_file(out / "meta.json", meta.dump() + "\n")) {
        err = "can't write meta.json";
        return false;
    }
    return true;
}

bool commit_docset(const std::string& root,
                   const std::string& slug,
                   const std::string& tmp,
                   std::string& err) {
    const fs::path target = fs::path(root) / slug;
    const fs::path old = fs::path(root) / (".old-" + slug);
    std::error_code ec;
    fs::remove_all(old, ec);
    const bool had = fs::exists(target, ec);
    if(had) {
        fs::rename(target, old, ec);
        if(ec) {
            err = "can't move the old copy aside: " + ec.message();
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if(ec) {
        err = "can't move the new copy into place: " + ec.message();
        if(had) {
            std::error_code ec2;
            fs::rename(old, target, ec2);  // put the previous good copy back
        }
        return false;
    }
    fs::remove_all(old, ec);
    return true;
}

bool remove_docset(const std::string& root,
                   const std::string& slug,
                   std::string& err) {
    // valid_slug + meta.json: never rm -rf anything that isn't a finished
    // install
    if(!valid_slug(slug) || !read_meta((fs::path(root) / slug).string())) {
        err = slug + " is not installed";
        return false;
    }
    std::error_code ec;
    fs::remove_all(fs::path(root) / slug, ec);
    if(ec) {
        err = "can't delete " + slug + ": " + ec.message();
        return false;
    }
    std::vector<Recent> all = load_recent(root);
    const size_t before = all.size();
    std::erase_if(all, [&](const Recent& r) { return r.slug == slug; });
    if(all.size() != before) {
        std::string out;
        for(const Recent& x : all)
            out += x.slug + "\t" + x.name + "\t" + x.target + "\n";
        write_atomic(fs::path(root) / "recent.state", out);
    }
    return write_level0(root, err);
}

bool write_level0(const std::string& root, std::string& err) {
    const std::vector<DocsetInfo> all = installed(root);
    std::string md = "# Documentation\n\n";
    bool any_html = false;
    if(all.empty())
        md += "No docsets installed. `:DocInstall <slug>` fetches one.\n";
    for(const DocsetInfo& d : all) {
        any_html = any_html || d.html;
        md += "- [" + md_escape(d.name.empty() ? d.slug : d.name) + "](" +
              pct_encode(d.slug) + "/INDEX.md) \xE2\x80\x94 " + d.slug;
        if(!d.version.empty()) md += " \xC2\xB7 version " + d.version;
        md += " \xC2\xB7 " + with_commas(d.entries) +
              " entries \xC2\xB7 installed " + d.installed + "\n";
    }
    std::error_code ec;
    fs::create_directories(root, ec);
    if(!write_atomic(fs::path(root) / "index.md", md)) {
        err = "can't write index.md";
        return false;
    }
    const fs::path hp = fs::path(root) / "index.html";
    if(!any_html) {
        fs::remove(hp, ec);
        return true;
    }
    std::string h =
        html_head("Documentation") + "<h1>Documentation</h1>\n<ul>\n";
    for(const DocsetInfo& d : all) {
        if(!d.html) continue;
        h += "<li><a href=\"" + html_escape_text(pct_encode(d.slug)) +
             "/INDEX.html\">" +
             html_escape_text(d.name.empty() ? d.slug : d.name) +
             "</a> \xE2\x80\x94 " + html_escape_text(d.slug) + " \xC2\xB7 " +
             with_commas(d.entries) + " entries</li>\n";
    }
    h += "</ul>\n</body></html>\n";
    if(!write_atomic(hp, h)) {
        err = "can't write index.html";
        return false;
    }
    return true;
}

// ---- install job ------------------------------------------------------------

namespace {
std::string sh_quote(std::string_view s) {
    std::string o = "'";
    for(const char c : s) {
        if(c == '\'') o += "'\\''";
        else o += c;
    }
    return o + "'";
}
}  // namespace

std::string install_script(const ScriptOpts& o) {
    const Docset* src = source_for(slug_base(o.slug));
    const Docset* jd = javadoc_for(slug_base(o.slug));
    std::string s;
    s += "EXE=" + sh_quote(o.exe) + "\nREQ=" + sh_quote(o.slug) +
         "\nROOT=" + sh_quote(o.root) + "\nCDN=" + sh_quote(o.cdn) +
         "\nMANIFEST=" + sh_quote(o.manifest) +
         "\nHTML=" + (o.html ? "1" : "0") +
         "\nUPDATE=" + (o.update ? "1" : "0") +
         "\nFORCE=" + (o.force ? "1" : "0") + "\nREV=\n";
    s += R"SH(fail() { echo "FAILED: $1"; exit 1; }
command -v curl >/dev/null 2>&1 || fail "curl not found"
mkdir -p "$ROOT" || fail "can't create $ROOT"
rm -rf "$ROOT"/.tmp-"$REQ"-*
TMP=$(mktemp -d "$ROOT/.tmp-$REQ-XXXXXX") || fail "can't create a temporary directory"
trap 'rm -rf "$TMP"' EXIT
mkdir "$TMP/dl" || fail "can't create $TMP/dl"
echo "fetching the docset list"
curl -fsSL --max-time 60 "$MANIFEST" -o "$TMP/dl/docs.json" 2>/dev/null || rm -f "$TMP/dl/docs.json"
SLUG=$("$EXE" --doc-resolve "$REQ" "$TMP/dl/docs.json") || fail "resolving $REQ"
if grep -q '"slug"' "$TMP/dl/docs.json" 2>/dev/null; then
  cp "$TMP/dl/docs.json" "$ROOT/catalog.json.new" && mv "$ROOT/catalog.json.new" "$ROOT/catalog.json"
fi
)SH";
    if(src) {
        // the head commit is both the freshness stamp and what gets downloaded
        s += "REPO=" + sh_quote(std::string(src->repo)) +
             "\nREF=" + sh_quote(std::string(src->ref)) + "\n";
        s += R"SH(command -v git >/dev/null 2>&1 || fail "git not found"
command -v tar >/dev/null 2>&1 || fail "tar not found"
echo "checking $REPO@$REF"
REV=$(GIT_TERMINAL_PROMPT=0 git ls-remote "https://github.com/$REPO.git" "refs/heads/$REF" | cut -f1 | head -n 1)
[ -n "$REV" ] || fail "resolving $REPO@$REF"
)SH";
    }
    if(jd) {
        // Javadoc the build tools already fetched (Maven, Gradle, distro) comes
        // first; the version found is both the freshness stamp and the slug
        // suffix ("lwjgl" -> "lwjgl~3.3.6"). Without a local copy: Maven
        // Central, at the version the slug names, else the newest release.
        s += "COORDS=" + sh_quote(std::string(jd->coords)) + "\nLOCAL=0\n";
        s += R"SH(MAVEN="${WINTER_DOCS_MAVEN:-https://repo1.maven.org/maven2}"
command -v unzip >/dev/null 2>&1 || fail "unzip not found"
echo "looking for local Javadoc"
if "$EXE" --doc-locate "$REQ" > "$TMP/dl/local.tsv"; then
  LOCAL=1
  REV=$(sed -n 1p "$TMP/dl/local.tsv")
  sed 1d "$TMP/dl/local.tsv" > "$TMP/dl/jars.tsv"
  [ -n "$REV" ] || fail "reading the local Javadoc version"
else
  case "$REQ" in
  *~*) REV=${REQ#*~} ;;
  *)
    set -- $COORDS
    G=${1%%:*}; A=${1##*:}
    echo "checking $A"
    REV=$(curl -fsSL --max-time 60 "$MAVEN/$(echo "$G" | tr . /)/$A/maven-metadata.xml" | sed -n 's:.*<release>\(.*\)</release>.*:\1:p' | head -n 1)
    [ -n "$REV" ] || fail "resolving $A on Maven Central"
    ;;
  esac
fi
SLUG="${REQ%%~*}~$REV"
)SH";
    }
    s += R"SH(if [ "$UPDATE" = 1 ] && [ "$FORCE" = 0 ]; then
  if "$EXE" --doc-check "$SLUG" "$ROOT" "$TMP/dl/docs.json" "$HTML" "$REV"; then
    echo "$SLUG is up to date"
    exit 0
  fi
fi
)SH";
    if(src) {
        s += R"SH(mkdir "$TMP/src" || fail "can't create $TMP/src"
echo "downloading $SLUG"
curl -fsSL --retry 2 --max-time 900 "https://codeload.github.com/$REPO/tar.gz/$REV" | tar -xz -C "$TMP/src" --strip-components=1 || fail "downloading $SLUG"
)SH";
    } else if(jd) {
        s += R"SH(mkdir "$TMP/src" || fail "can't create $TMP/src"
if [ "$LOCAL" = 1 ]; then
  while IFS="$(printf '\t')" read -r a p; do
    [ -n "$a" ] || continue
    echo "using $a $REV"
    mkdir "$TMP/src/$a" || fail "can't create $TMP/src/$a"
    if [ -d "$p" ]; then
      cp -R "$p/." "$TMP/src/$a/" || fail "copying $a"
    else
      unzip -q "$p" -d "$TMP/src/$a" || fail "unpacking $a"
    fi
  done < "$TMP/dl/jars.tsv"
else
  for c in $COORDS; do
    g=${c%%:*}; a=${c##*:}
    echo "downloading $a $REV"
    curl -fsSL --retry 2 --max-time 300 "$MAVEN/$(echo "$g" | tr . /)/$a/$REV/$a-$REV-javadoc.jar" -o "$TMP/dl/$a.jar" || fail "downloading $a-$REV-javadoc.jar"
    mkdir "$TMP/src/$a" && unzip -q "$TMP/dl/$a.jar" -d "$TMP/src/$a" || fail "unpacking $a"
  done
fi
)SH";
    } else {
        s += R"SH(echo "downloading $SLUG index.json"
curl -fsSL --retry 2 --max-time 120 "$CDN/$SLUG/index.json" -o "$TMP/dl/index.json" || fail "downloading $SLUG index.json (not a DevDocs slug?)"
echo "downloading $SLUG db.json"
curl -fsSL --retry 2 --max-time 900 "$CDN/$SLUG/db.json" -o "$TMP/dl/db.json" || fail "downloading $SLUG db.json"
)SH";
    }
    s +=
        R"SH(WINTER_DOCS_REV="$REV" "$EXE" --doc-build "$SLUG" "$TMP" "$ROOT" "$HTML" || exit 1
)SH";
    return s;
}

std::string catalog_script(const ScriptOpts& o) {
    std::string s = "ROOT=" + sh_quote(o.root) +
                    "\nMANIFEST=" + sh_quote(o.manifest) + "\n";
    s += R"SH(fail() { echo "FAILED: $1"; exit 1; }
command -v curl >/dev/null 2>&1 || fail "curl not found"
mkdir -p "$ROOT" || fail "can't create $ROOT"
echo "fetching the docset list"
curl -fsSL --retry 2 --max-time 60 "$MANIFEST" -o "$ROOT/catalog.json.new" || { rm -f "$ROOT/catalog.json.new"; fail "downloading the docset list"; }
grep -q '"slug"' "$ROOT/catalog.json.new" || { rm -f "$ROOT/catalog.json.new"; fail "the docset list is not DevDocs' docs.json"; }
mv "$ROOT/catalog.json.new" "$ROOT/catalog.json" || fail "saving the docset list"
echo "docset list updated: $(grep -o '"slug"' "$ROOT/catalog.json" | wc -l) docsets"
)SH";
    return s;
}

// ---- library sources: import ------------------------------------------------

namespace {

std::string json_str(std::string_view s) {
    std::string o = "\"";
    for(const unsigned char c : s) {
        switch(c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if(c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o + "\"";
}

// First "# ..." line with any <tags> stripped:
// "# <small>nlohmann::basic_json::</small>push_back" ->
// "nlohmann::basic_json::push_back"
std::string md_title(std::string_view md) {
    size_t i = md.starts_with("# ") ? 0 : md.find("\n# ");
    if(i == std::string_view::npos) return {};
    i = md.find('#', i) + 2;
    const size_t e = md.find('\n', i);
    std::string t;
    bool tag = false;
    for(const char c : md.substr(i, e == std::string_view::npos ? e : e - i)) {
        if(c == '<') tag = true;
        else if(c == '>') tag = false;
        else if(!tag) t += c;
    }
    while(!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

// Point relative links at real pages: add ".md" where it's missing and keep
// any #fragment. Targets that don't resolve to a page are left alone.
std::string fix_links(std::string_view md,
                      const std::string& key,
                      const std::unordered_set<std::string>& keys) {
    std::string o;
    const fs::path dir = fs::path(key).parent_path();
    size_t i = 0;
    while(i < md.size()) {
        const size_t p = md.find("](", i);
        if(p == std::string_view::npos) {
            o.append(md.substr(i));
            break;
        }
        const size_t e = md.find(')', p + 2);
        if(e == std::string_view::npos) {
            o.append(md.substr(i));
            break;
        }
        o.append(md.substr(i, p + 2 - i));
        std::string tgt(md.substr(p + 2, e - p - 2));
        std::string frag;
        if(const size_t h = tgt.find('#'); h != std::string::npos) {
            frag = tgt.substr(h);
            tgt.resize(h);
        }
        if(!tgt.empty() && tgt.find("://") == std::string::npos &&
           tgt.find(' ') == std::string::npos) {
            const std::string k0 =
                tgt.ends_with(".md") ? tgt.substr(0, tgt.size() - 3) : tgt;
            const std::string k =
                (dir / k0).lexically_normal().generic_string();
            if(keys.contains(k))
                tgt = fs::path(k)
                          .lexically_relative(dir.empty() ? fs::path(".") : dir)
                          .generic_string() +
                      ".md";
        }
        o += tgt + frag + ")";
        i = e + 1;
    }
    return o;
}

}  // namespace

bool import_tree(const Docset& s,
                 const std::string& src_dir,
                 const std::string& dl_dir,
                 std::string& err) {
    const fs::path root = fs::path(src_dir) / std::string(s.subdir);
    std::error_code ec;
    if(!fs::is_directory(root, ec)) {
        err = "'" + std::string(s.subdir) + "' is not in the downloaded source";
        return false;
    }
    struct Page {
        std::string key, text;
    };
    std::vector<Page> pages;
    std::unordered_set<std::string> keys;
    for(auto it = fs::recursive_directory_iterator(root, ec);
        !ec && it != fs::recursive_directory_iterator();
        it.increment(ec)) {
        if(!it->is_regular_file() || it->path().extension() != ".md") continue;
        std::string text;
        if(!read_file(it->path().string(), text) || text.empty()) continue;
        std::string key = fs::relative(it->path(), root).generic_string();
        key.resize(key.size() - 3);
        keys.insert(key);
        pages.push_back({std::move(key), std::move(text)});
    }
    if(pages.empty()) {
        err = "no markdown pages found";
        return false;
    }
    std::sort(pages.begin(), pages.end(), [](const Page& a, const Page& b) {
        return a.key < b.key;
    });

    std::string idx = "{\"entries\":[", db = "{";
    bool first_e = true, first_p = true;
    const auto add_entry = [&](const std::string& name,
                               const std::string& key,
                               const std::string& type) {
        idx += std::string(first_e ? "" : ",") + "{\"name\":" + json_str(name) +
               ",\"path\":" + json_str(key) + ",\"type\":" + json_str(type) +
               "}";
        first_e = false;
    };
    for(const Page& p : pages) {
        std::string name = md_title(p.text);
        if(name.empty()) name = fs::path(p.key).filename().string();
        const size_t sl = p.key.find('/');
        const std::string type =
            sl == std::string::npos ? "API" : p.key.substr(0, sl);

        add_entry(name, p.key, type);
        std::string bare = name;  // without the namespace prefix
        if(!s.strip_ns.empty() && bare.starts_with(s.strip_ns))
            bare.erase(0, s.strip_ns.size());
        if(bare != name) add_entry(bare, p.key, type);
        if(!s.alias_from.empty())  // basic_json::x -> json::x
            for(const std::string* b : {&name, &bare})
                if(const size_t f = b->find(s.alias_from);
                   f != std::string::npos) {
                    std::string a = *b;
                    a.replace(f, s.alias_from.size(), s.alias_to);
                    add_entry(a, p.key, type);
                }

        db += std::string(first_p ? "" : ",") + json_str(p.key) + ":" +
              json_str(fix_links(p.text, p.key, keys));
        first_p = false;
    }
    idx += "]}";
    db += "}";
    if(!write_file(fs::path(dl_dir) / "index.json", idx) ||
       !write_file(fs::path(dl_dir) / "db.json", db)) {
        err = "can't write the converted source";
        return false;
    }
    return true;
}

namespace {

// "[...]" out of a Javadoc search index (`typeSearchIndex = [...];update...`)
std::optional<Json> load_search_index(const fs::path& p) {
    std::string t;
    if(!read_file(p.string(), t)) return std::nullopt;
    const size_t a = t.find('['), b = t.rfind(']');
    if(a == std::string::npos || b == std::string::npos || b < a)
        return std::nullopt;
    auto j = Json::parse(std::string_view(t).substr(a, b - a + 1));
    if(!j || !j->is_array()) return std::nullopt;
    return j;
}

// Drop `<open ...> ... </open>` blocks whose opening tag starts with `open`,
// matching nested tags of the same name.
std::string
drop_blocks(std::string_view h, std::string_view open, std::string_view tag) {
    const std::string o1 = "<" + std::string(tag),
                      c1 = "</" + std::string(tag) + ">";
    std::string out;
    size_t i = 0;
    while(i < h.size()) {
        const size_t p = h.find(open, i);
        if(p == std::string_view::npos) {
            out.append(h.substr(i));
            break;
        }
        out.append(h.substr(i, p - i));
        int depth = 0;
        size_t q = p;
        while(q < h.size()) {
            const size_t no = h.find(o1, q), nc = h.find(c1, q);
            if(nc == std::string_view::npos) {
                q = h.size();
                break;
            }
            if(no != std::string_view::npos && no < nc) {
                ++depth;
                q = no + o1.size();
            } else {
                --depth;
                q = nc + c1.size();
                if(depth == 0) break;
            }
        }
        i = q;
    }
    return out;
}

// <div class="member-signature">..</div> (no nested div) -> <pre>..</pre>
std::string signatures_to_pre(std::string_view h) {
    std::string out;
    size_t i = 0;
    while(i < h.size()) {
        size_t p = std::string_view::npos;
        size_t plen = 0;
        for(const std::string_view k : {"<div class=\"member-signature\">",
                                        "<div class=\"type-signature\">"}) {
            const size_t f = h.find(k, i);
            if(f < p) {
                p = f;
                plen = k.size();
            }
        }
        if(p == std::string_view::npos) {
            out.append(h.substr(i));
            break;
        }
        const size_t e = h.find("</div>", p + plen);
        if(e == std::string_view::npos) {
            out.append(h.substr(i));
            break;
        }
        out.append(h.substr(i, p - i));
        std::string body(h.substr(p + plen, e - p - plen));
        for(size_t n; (n = body.find("&nbsp;")) != std::string::npos;)
            body.replace(n, 6, " ");
        out += "<pre>" + body + "</pre>";
        i = e + 6;
    }
    return out;
}

// <section class="detail" id="X"> <h3>..  ->  <h3 id="X">..: the converter
// reads an anchor off the heading, and Javadoc puts it on the section.
std::string hoist_ids(std::string_view h) {
    constexpr std::string_view k = "<section class=\"detail\" id=\"";
    std::string out;
    size_t i = 0;
    while(i < h.size()) {
        const size_t p = h.find(k, i);
        if(p == std::string_view::npos) {
            out.append(h.substr(i));
            break;
        }
        const size_t ie = h.find('"', p + k.size());
        size_t q = ie == std::string_view::npos ? ie : h.find('>', ie);
        if(q == std::string_view::npos) {
            out.append(h.substr(i));
            break;
        }
        const std::string_view id = h.substr(p + k.size(), ie - p - k.size());
        size_t n = q + 1;
        while(n < h.size() && std::isspace(static_cast<unsigned char>(h[n])))
            ++n;
        out.append(h.substr(i, q + 1 - i));
        if(h.substr(n, 2) == "<h" && n + 3 < h.size() && h[n + 3] == '>') {
            out.append(h.substr(q + 1, n - q - 1));
            out.append(h.substr(n, 3));
            out += " id=\"" + std::string(id) + "\"";
            i = n + 3;
        } else {
            i = q + 1;
        }
    }
    return out;
}

// The member lists are <ul><li><section>..: markdown would indent every member
// under a bullet. Make them plain divs.
std::string unwrap_lists(std::string_view h) {
    std::string s(h);
    for(const std::string_view a : {"<li>\n<section", "<li><section"})
        for(size_t n; (n = s.find(a)) != std::string::npos;)
            s.replace(n, 4, "<div>");
    for(const std::string_view a : {"</section>\n</li>", "</section></li>"})
        for(size_t n; (n = s.find(a)) != std::string::npos;)
            s.replace(s.find("</li>", n), 5, "</div>");
    std::string out;
    std::vector<bool> stack;  // per open <ul>: was it turned into a div
    size_t i = 0;
    while(i < s.size()) {
        const size_t o = s.find("<ul", i), c = s.find("</ul>", i);
        if(o == std::string::npos && c == std::string::npos) {
            out.append(s, i, std::string::npos);
            break;
        }
        if(o != std::string::npos && (c == std::string::npos || o < c)) {
            const size_t e = s.find('>', o);
            if(e == std::string::npos) {
                out.append(s, i, std::string::npos);
                break;
            }
            const std::string_view tag =
                std::string_view(s).substr(o, e - o + 1);
            const bool plain = tag == "<ul class=\"details-list\">" ||
                               tag == "<ul class=\"member-list\">" ||
                               tag == "<ul class=\"summary-list\">";
            stack.push_back(plain);
            out.append(s, i, o - i);
            out += plain ? "<div>" : std::string(tag);
            i = e + 1;
        } else {
            const bool plain = !stack.empty() && stack.back();
            if(!stack.empty()) stack.pop_back();
            out.append(s, i, c - i);
            out += plain ? "</div>" : "</ul>";
            i = c + 5;
        }
    }
    return out;
}

// LWJGL's GL11C, GL45C...: the core-profile twins of GL11, GL45... Their
// members carry only the C prototype, so they would shadow the documented
// ones. Their pages stay (GL11 links to them for its constants).
bool is_core_twin(std::string_view cls) {
    if(!cls.starts_with("GL") || !cls.ends_with('C') || cls.size() < 4)
        return false;
    for(const char c : cls.substr(2, cls.size() - 3))
        if(!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// The <main> of a Javadoc page, reduced to what reads well as markdown.
std::string javadoc_body(std::string_view page) {
    const size_t a = page.find("<main");
    const size_t b = page.rfind("</main>");
    if(a == std::string_view::npos || b == std::string_view::npos || b < a)
        return {};
    const size_t gt = page.find('>', a);
    std::string h(page.substr(gt + 1, b - gt - 1));
    h = drop_blocks(h, "<section class=\"summary\">", "section");
    h = drop_blocks(h, "<button", "button");
    h = signatures_to_pre(h);
    h = unwrap_lists(h);
    return hoist_ids(h);
}

}  // namespace

bool import_javadoc(const Docset& s,
                    const std::string& src_dir,
                    const std::string& dl_dir,
                    std::string& err) {
    (void)s;
    std::error_code ec;
    std::vector<fs::path> mods;
    for(const auto& d : fs::directory_iterator(src_dir, ec))
        if(d.is_directory()) mods.push_back(d.path());
    std::sort(mods.begin(), mods.end());

    std::string idx = "{\"entries\":[", db = "{";
    bool first_e = true, first_p = true;
    std::unordered_set<std::string> have;  // page keys already in db
    const auto add_entry = [&](const std::string& name,
                               const std::string& path,
                               const std::string& type) {
        idx += std::string(first_e ? "" : ",") + "{\"name\":" + json_str(name) +
               ",\"path\":" + json_str(path) + ",\"type\":" + json_str(type) +
               "}";
        first_e = false;
    };
    // a page's HTML goes in once; false when the page is missing or empty
    const auto add_page = [&](const fs::path& mod, const std::string& key) {
        if(have.contains(key)) return true;
        std::string html;
        if(!read_file((mod / (key + ".html")).string(), html)) return false;
        const std::string body = javadoc_body(html);
        if(body.empty()) return false;
        db += std::string(first_p ? "" : ",") + json_str(key) + ":" +
              json_str(body);
        first_p = false;
        have.insert(key);
        return true;
    };
    const auto pkg_path = [](std::string p) {
        std::replace(p.begin(), p.end(), '.', '/');
        return p;
    };

    for(const fs::path& mod : mods) {
        // packages
        if(const auto pk = load_search_index(mod / "package-search-index.js"))
            for(const Json& e : pk->items()) {
                const std::string p(e["l"].as_str());
                if(p.empty() || !e["u"].as_str().empty()) continue;
                const std::string key = pkg_path(p) + "/package-summary";
                if(add_page(mod, key)) add_entry(p, key, "Package");
            }
        // types
        std::unordered_set<std::string>
            type_pages;  // "pkg.Class" -> has a page
        if(const auto ty = load_search_index(mod / "type-search-index.js"))
            for(const Json& e : ty->items()) {
                const std::string p(e["p"].as_str()), l(e["l"].as_str());
                if(p.empty() || l.empty()) continue;
                const std::string key = pkg_path(p) + "/" + l;
                if(!add_page(mod, key)) continue;
                type_pages.insert(p + "." + l);
                add_entry(p + "." + l, key, p);
                add_entry(l, key, p);
            }
        // members; a bare name is only indexed when one class owns it
        struct Mem {
            std::string base, cls, key, anchor, pkg;
        };
        std::vector<Mem> mems;
        std::map<std::string, std::set<std::string>> owners;
        if(const auto me = load_search_index(mod / "member-search-index.js"))
            for(const Json& e : me->items()) {
                const std::string p(e["p"].as_str()), c(e["c"].as_str()),
                    l(e["l"].as_str());
                if(p.empty() || c.empty() || l.empty() || is_core_twin(c) ||
                   !type_pages.contains(p + "." + c))
                    continue;
                std::string anchor(e["u"].as_str());
                if(anchor.empty()) anchor = l;
                const std::string base = l.substr(0, l.find('('));
                mems.push_back({base, c, pkg_path(p) + "/" + c, anchor, p});
                owners[base].insert(p + "." + c);
            }
        for(const Mem& m : mems) {
            const std::string path = m.key + "#" + m.anchor;
            add_entry(m.cls + "." + m.base, path, m.pkg);
            add_entry(m.pkg + "." + m.cls + "." + m.base, path, m.pkg);
            if(owners[m.base].size() == 1) add_entry(m.base, path, m.pkg);
        }
    }
    if(first_p) {
        err = "no Javadoc pages found (unexpected jar layout?)";
        return false;
    }
    idx += "]}";
    db += "}";
    if(!write_file(fs::path(dl_dir) / "index.json", idx) ||
       !write_file(fs::path(dl_dir) / "db.json", db)) {
        err = "can't write the converted source";
        return false;
    }
    return true;
}

// ---- cli --------------------------------------------------------------------

namespace {

// "6.5" < "6.10": numeric where both sides are
bool version_less(std::string_view a, std::string_view b) {
    const auto parts = [](std::string_view s) {
        std::vector<std::string> p;
        std::string cur;
        for(const char c : s) {
            if(c == '.' || c == '-' || c == '~') {
                p.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        p.push_back(cur);
        return p;
    };
    const auto pa = parts(a), pb = parts(b);
    for(size_t i = 0; i < std::min(pa.size(), pb.size()); ++i) {
        const bool na = !pa[i].empty() &&
                        std::all_of(pa[i].begin(), pa[i].end(), ::isdigit);
        const bool nb = !pb[i].empty() &&
                        std::all_of(pb[i].begin(), pb[i].end(), ::isdigit);
        if(na && nb) {
            const long x = std::strtol(pa[i].c_str(), nullptr, 10);
            const long y = std::strtol(pb[i].c_str(), nullptr, 10);
            if(x != y) return x < y;
        } else if(pa[i] != pb[i]) {
            return pa[i] < pb[i];
        }
    }
    return pa.size() < pb.size();
}

std::optional<Json> load_manifest(const std::string& path) {
    std::string t;
    if(!read_file(path, t)) return std::nullopt;
    auto j = Json::parse(t);
    if(!j || !j->is_array()) return std::nullopt;
    return j;
}

const Json* manifest_entry(const Json& m, std::string_view slug) {
    for(const Json& e : m.items())
        if(e["slug"].as_str() == slug) return &e;
    return nullptr;
}

}  // namespace

std::vector<Avail> load_catalog(const std::string& root) {
    std::vector<Avail> out;
    if(const auto m = load_manifest(root + "/catalog.json"))
        for(const Json& e : m->items()) {
            Avail a;
            a.slug = std::string(e["slug"].as_str());
            if(a.slug.empty() || !valid_slug(a.slug)) continue;
            a.name = std::string(e["name"].as_str());
            a.version = std::string(e["release"].as_str());
            if(a.version.empty())
                a.version = std::string(e["version"].as_str());
            out.push_back(std::move(a));
        }
    return out;
}

// ---- local Javadoc
// ------------------------------------------------------------

namespace {

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

// ~/.m2/settings.xml's <localRepository> (maven.repo.local), else
// ~/.m2/repository. XML comments are skipped: the stock settings.xml carries
// a commented-out example of that very element.
std::string maven_local_repo() {
    if(const char* e = std::getenv("WINTER_DOCS_MAVEN_REPO"); e && *e) return e;
    const std::string home = home_dir();
    std::string xml;
    if(read_file(home + "/.m2/settings.xml", xml)) {
        for(size_t a; (a = xml.find("<!--")) != std::string::npos;) {
            const size_t b = xml.find("-->", a);
            xml.erase(a, b == std::string::npos ? b : b + 3 - a);
        }
        const size_t a = xml.find("<localRepository>");
        const size_t b = xml.find("</localRepository>");
        if(a != std::string::npos && b != std::string::npos && b > a) {
            std::string p = xml.substr(a + 17, b - a - 17);
            const size_t f = p.find_first_not_of(" \t\r\n");
            const size_t l = p.find_last_not_of(" \t\r\n");
            p = f == std::string::npos ? "" : p.substr(f, l - f + 1);
            if(p.starts_with("${user.home}")) p = home + p.substr(12);
            if(p.starts_with('/')) return p;
        }
    }
    return home + "/.m2/repository";
}

std::vector<fs::path> subdirs(const fs::path& d) {
    std::vector<fs::path> v;
    std::error_code ec;
    for(const auto& e : fs::directory_iterator(d, ec))
        if(e.is_directory(ec)) v.push_back(e.path());
    return v;
}

}  // namespace

std::vector<JavadocFind> find_javadoc(const Docset& s) {
    std::map<std::string, std::vector<JavadocSrc>> by_ver;
    // first hit per (version, artifact) wins: Maven, then Gradle, then the
    // system
    const auto add =
        [&](const std::string& ver, const std::string& art, const fs::path& p) {
            const std::string path = p.string();
            if(path.find('\t') != std::string::npos ||
               path.find('\n') != std::string::npos ||
               !valid_slug(std::string(s.slug) + "~" + ver))
                return;
            auto& v = by_ver[ver];
            for(const JavadocSrc& o : v)
                if(o.artifact == art) return;
            v.push_back({art, path});
        };
    const std::string gh = [] {
        const char* e = std::getenv("GRADLE_USER_HOME");
        return e && *e ? std::string(e) : home_dir() + "/.gradle";
    }();
    const std::string m2 = maven_local_repo();

    std::error_code ec;
    size_t i = 0;
    while(i < s.coords.size()) {
        size_t e = s.coords.find(' ', i);
        if(e == std::string_view::npos) e = s.coords.size();
        const std::string c(s.coords.substr(i, e - i));
        i = e + 1;
        const size_t colon = c.find(':');
        if(colon == std::string::npos) continue;
        std::string gpath = c.substr(0, colon);
        std::replace(gpath.begin(), gpath.end(), '.', '/');
        const std::string art = c.substr(colon + 1);
        const std::string jar = art + "-";

        // Maven layout: <repo>/<group
        // path>/<artifact>/<version>/<artifact>-<version>-javadoc.jar
        for(const std::string& repo :
            {m2, std::string("/usr/share/maven-repo")})
            for(const fs::path& vd : subdirs(fs::path(repo) / gpath / art)) {
                const std::string ver = vd.filename().string();
                const fs::path f = vd / (jar + ver + "-javadoc.jar");
                if(fs::is_regular_file(f, ec)) add(ver, art, f);
            }
        // Gradle cache:
        // <group>/<artifact>/<version>/<hash>/<artifact>-<version>-javadoc.jar
        for(const fs::path& vd :
            subdirs(fs::path(gh) / "caches/modules-2/files-2.1" /
                    c.substr(0, colon) / art))
            for(const fs::path& hd : subdirs(vd)) {
                const std::string ver = vd.filename().string();
                const fs::path f = hd / (jar + ver + "-javadoc.jar");
                if(fs::is_regular_file(f, ec)) add(ver, art, f);
            }
        // distro packages unpack the HTML: no version to read, so "system"
        const fs::path sys = fs::path("/usr/share/javadoc") / art;
        if(fs::is_regular_file(sys / "type-search-index.js", ec))
            add("system", art, sys);
    }

    std::vector<JavadocFind> out;
    for(auto& [ver, srcs] : by_ver) out.push_back({ver, std::move(srcs)});
    std::sort(
        out.begin(), out.end(), [](const JavadocFind& a, const JavadocFind& b) {
            if((a.version == "system") != (b.version == "system"))
                return b.version == "system";           // unversioned last
            return version_less(b.version, a.version);  // newest first
        });
    return out;
}

namespace {

std::string env_or(const char* k, const char* def) {
    const char* v = std::getenv(k);
    return v && *v ? v : def;
}

void say_line(const std::string& s) {
    std::puts(s.c_str());
    std::fflush(stdout);
}

int cli_resolve(int argc, const char* argv[]) {
    if(argc < 4) return 2;
    const std::string slug = argv[2];
    std::string pick = slug;
    if(const auto m = load_manifest(argv[3]); m && !manifest_entry(*m, slug)) {
        std::string best;
        for(const Json& e : m->items()) {
            const std::string s(e["slug"].as_str());
            if(s.size() > slug.size() + 1 && s.starts_with(slug + "~") &&
               (best.empty() || version_less(best.substr(slug.size() + 1),
                                             s.substr(slug.size() + 1))))
                best = s;
        }
        if(!best.empty()) pick = best;
    }
    std::puts(pick.c_str());
    return 0;
}

// The stamp kept in meta.json for an MdTree docset: the commit, shortened.
std::string short_rev(std::string_view rev) {
    return std::string(rev.substr(0, 12));
}

// --doc-locate <slug>: the Javadoc a project's build tools already fetched.
// Prints the version, then one "<artifact>\t<jar or directory>" line per
// module. "lwjgl" takes the newest local version, "lwjgl~3.3.6" that one. Exit
// 1 when nothing is found locally (the install script then falls back to Maven
// Central).
int cli_locate(int argc, const char* argv[]) {
    if(argc < 3) return 2;
    const std::string req = argv[2];
    const Docset* jd = javadoc_for(slug_base(req));
    if(!jd) return 1;
    const size_t t = req.find('~');
    const std::string want = t == std::string::npos ? "" : req.substr(t + 1);
    for(const JavadocFind& f : find_javadoc(*jd)) {
        if(!want.empty() && f.version != want) continue;
        std::puts(f.version.c_str());
        for(const JavadocSrc& src : f.srcs)
            std::printf("%s\t%s\n", src.artifact.c_str(), src.path.c_str());
        return 0;
    }
    return 1;
}

int cli_check(int argc, const char* argv[]) {  // 0 = up to date
    if(argc < 6) return 2;
    const std::string slug = argv[2], root = argv[3];
    const bool html = std::string_view(argv[5]) == "1";
    const auto meta = read_meta(root + "/" + slug);
    if(source_for(slug_base(slug)) || javadoc_for(slug_base(slug))) {
        // no manifest entry: compare the commit / Maven release
        std::string rev = argc > 6 ? argv[6] : "";
        if(source_for(slug_base(slug))) rev = short_rev(rev);
        return meta && !rev.empty() && meta->version == rev ? 0 : 1;
    }
    const auto m = load_manifest(argv[4]);
    if(!meta || !m) return 1;
    const Json* e = manifest_entry(*m, slug);
    if(!e || (*e)["mtime"].as_int() == 0) return 1;
    return (*e)["mtime"].as_int() == meta->mtime && meta->html == html ? 0 : 1;
}

int cli_build(int argc, const char* argv[]) {
    if(argc < 6) return 2;
    const std::string slug = argv[2], tmp = argv[3], root = argv[4];
    if(!valid_slug(slug)) {
        say_line("FAILED: bad docset name");
        return 1;
    }
    const Docset* src = source_for(slug_base(slug));
    if(src) {
        std::string ierr;
        if(!import_tree(*src, tmp + "/src", tmp + "/dl", ierr)) {
            say_line("FAILED: " + ierr);
            return 1;
        }
    }
    const Docset* jd = javadoc_for(slug_base(slug));
    if(jd) {
        std::string ierr;
        if(!import_javadoc(*jd, tmp + "/src", tmp + "/dl", ierr)) {
            say_line("FAILED: " + ierr);
            return 1;
        }
    }
    BuildOpts o;
    o.slug = slug;
    o.out_dir = tmp;
    o.dl_dir = tmp + "/dl";
    o.save_html = std::string_view(argv[5]) == "1";
    o.source = env_or("WINTER_DOCS_CDN", "https://documents.devdocs.io") + "/" +
               slug + "/";
    if(const auto m = load_manifest(o.dl_dir + "/docs.json"))
        if(const Json* e = manifest_entry(*m, slug)) {
            o.name = std::string((*e)["name"].as_str());
            o.version = std::string((*e)["release"].as_str());
            if(o.version.empty())
                o.version = std::string((*e)["version"].as_str());
            o.mtime = (*e)["mtime"].as_int();
        }
    if(src) {
        o.markdown = true;
        o.save_html = false;  // nothing to save: the pages are markdown already
        o.name = std::string(src->name);
        o.version = short_rev(env_or("WINTER_DOCS_REV", ""));
        o.source = "https://github.com/" + std::string(src->repo) + "/tree/" +
                   std::string(src->ref) + "/" + std::string(src->subdir);
    }
    if(jd) {  // the pages are HTML fragments, so markdown stays false
        std::string first(jd->coords.substr(0, jd->coords.find(' ')));
        if(const size_t c = first.find(':'); c != std::string::npos)
            first[c] = '/';
        o.name = std::string(jd->name);
        o.version = env_or("WINTER_DOCS_REV", "");
        o.source = "https://central.sonatype.com/artifact/" + first;
    }
    // a hand-written topics.tsv outlives updates
    std::error_code ec;
    const fs::path old_topics = fs::path(root) / slug / "topics.tsv";
    if(fs::exists(old_topics, ec))
        fs::copy_file(old_topics, fs::path(tmp) / "topics.tsv", ec);

    BuildStats st;
    std::string err;
    if(!build_docset(o, say_line, st, err)) {
        say_line("FAILED: " + err);
        return 1;
    }
    {  // the unpacked source is only the importer's input
        std::error_code rec;
        fs::remove_all(fs::path(tmp) / "src", rec);
    }
    say_line("installing");
    if(!commit_docset(root, slug, tmp, err) || !write_level0(root, err)) {
        say_line("FAILED: " + err);
        return 1;
    }
    say_line("installed " + slug + ": " + with_commas(st.entries) +
             " entries, " + with_commas(st.pages) + " pages");
    return 0;
}

}  // namespace

int cli_main(int argc, const char* argv[]) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if(cmd == "--doc-resolve") return cli_resolve(argc, argv);
    if(cmd == "--doc-check") return cli_check(argc, argv);
    if(cmd == "--doc-build") return cli_build(argc, argv);
    if(cmd == "--doc-locate") return cli_locate(argc, argv);
    return 2;
}

}  // namespace doc
