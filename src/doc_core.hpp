#pragma once
// Offline documentation (:Doc), editor-independent half.
//
// Nothing in here touches the editor: it is plain string/file logic so the
// same code runs inside the headless converter (`winter --doc-build`, spawned
// by :DocInstall / :DocUpdate through the job runner) and can be tested on its
// own. The editor half (commands, split, picker) lives in doc.cpp.
//
//   naming       slug <-> language table, filetype -> docset
//   token        the identifier under the cursor, separator normalisation
//   converter    DevDocs HTML -> markdown (doc_html.cpp)
//   lookup       index.tsv loading, exact and longest-prefix resolution
//   build        db.json + index.json -> the on-disk layout of the spec
//   cli          the --doc-* entry points
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace doc {

// ---- naming ---------------------------------------------------------------

// DevDocs slugs look like "cpp", "lua~5.4", "openjdk~21": letters, digits and
// . _ ~ -, no leading dot. Anything else never reaches a shell or a path.
bool valid_slug(std::string_view s);
std::string slug_base(std::string_view slug);  // "lua~5.4" -> "lua"

struct LangInfo {
    std::string_view base;   // slug before the '~'
    std::string_view fence;  // info string for <pre> blocks of this docset
    std::string_view sep;    // scope separator: "::" or "."
};
const LangInfo&
lang_for(std::string_view slug);  // unknown -> fence "", sep "."
// Slug bases to try for a buffer filetype, best first. Empty when none.
std::vector<std::string_view> bases_for_filetype(std::string_view ft);

// ---- cursor token ---------------------------------------------------------

// The name under byte column `col` of `line`, or "" when the cursor is not on
// one. Allowed: [A-Za-z0-9_] :: . ->  Stop: whitespace ( ) < > , ; [ ] quotes
// ('>' only continues a name directly after '-'). The name starts at the
// boundary before the cursor and ends with the segment the cursor is in:
// cursor on `filesystem` of `std::filesystem::path` gives `std::filesystem`.
std::string token_at(std::string_view line, size_t col);

// Rewrite the separators (-> . ::) to the docset's convention.
std::string normalize_name(std::string_view name, std::string_view sep);
// `a::b::c` -> {"a::b::c", "a::b", "a"}: the name, then each shorter scope.
std::vector<std::string> scope_prefixes(std::string_view name,
                                        std::string_view sep);

// ---- html -> markdown -----------------------------------------------------

struct ConvertCtx {
    std::string page;   // db.json key of the page being converted
    std::string fence;  // fallback info string for <pre>
    const std::unordered_set<std::string>* pages = nullptr;  // all db keys
};

// Small and DevDocs-specific on purpose: headings, paragraphs, lists, code,
// links, <dl>, <pre> and tables. Never throws.
std::string html_to_md(std::string_view html, const ConvertCtx& cx);
// The saved-HTML form of a page (save_html): the fragment in a minimal
// document, with cross-page links rewritten to the saved files.
std::string html_to_page(std::string_view html,
                         std::string_view title,
                         const ConvertCtx& cx);

// db.json key -> file path stem below the docset dir ("" = unusable key).
// Rejects "..", absolute and over-long paths and steps around the names the
// layout reserves (INDEX, html/).
std::string file_key(std::string_view key);
std::string pct_encode(std::string_view s);
std::string
md_escape(std::string_view s);  // text for a paragraph or link label
std::string html_escape_text(std::string_view s);  // & < > "

std::string pct_decode(std::string_view s);

// Line of the heading / term for `anchor` in a converted page, or -1.
int find_anchor(std::string_view md, std::string_view anchor);

// One sentence per source line (and a break after trailing "(since
// C++17)"-style groups), for reading. Code, links, tables, headings and fences
// are untouched; list and quote continuation lines keep their indent.
std::string semantic_breaks(std::string_view md);

// Pipe tables wider than `width` columns become lists (**name** with the
// description indented, or a bullet per row), so text can wrap instead of being
// cut. Narrower tables, and everything else, are untouched.
std::string reflow_tables(std::string_view md, int width);

// ---- lookup ---------------------------------------------------------------

struct Entry {
    std::string name, target,
        type;  // target: "<stem>.md" or "<stem>.md#anchor"
};
struct Index {
    std::string slug;
    std::vector<Entry> entries;  // sorted by name, byte order
};

bool load_index(const std::string& tsv_path, Index& out);
// All entries named exactly `name`. Case-sensitive first; when that finds
// nothing, a case-insensitive scan.
std::vector<const Entry*> find_exact(const Index& ix, std::string_view name);

// ---- installed docsets ----------------------------------------------------

struct DocsetInfo {
    std::string slug, name, version, installed;
    long long mtime = 0;
    size_t entries = 0, pages = 0;
    bool html = false;
};
// Every <root>/<slug>/ holding a meta.json (the install-complete marker).
std::vector<DocsetInfo> installed(const std::string& root);
std::optional<DocsetInfo> read_meta(const std::string& dir);

struct Recent {
    std::string slug, name, target;  // target relative to the docset dir
};
std::vector<Recent> load_recent(const std::string& root);
void note_recent(const std::string& root, const Recent& r);
// docs/index.md with the "Recently viewed" section spliced in at the top.
std::string splice_recent(std::string_view index_md,
                          const std::vector<Recent>& recent);

// ---- build ----------------------------------------------------------------

struct BuildOpts {
    std::string slug, name, version, source;
    long long mtime = 0;
    std::string dl_dir;   // holds index.json and db.json
    std::string out_dir;  // the temporary docset directory being filled
    bool save_html = false;
    bool markdown = false;  // db.json values are already markdown
};
struct BuildStats {
    size_t pages = 0, entries = 0, skipped_pages = 0, skipped_entries = 0;
};
using Progress = std::function<void(const std::string&)>;

// Steps 2 to 5 of the install pipeline: convert, write the pages, write
// index.tsv, the level 1 index and (last) meta.json into `out_dir`.
bool build_docset(const BuildOpts& o,
                  const Progress& say,
                  BuildStats& st,
                  std::string& err);
// Swap `tmp` in as <root>/<slug>. The previous good copy survives any failure.
bool commit_docset(const std::string& root,
                   const std::string& slug,
                   const std::string& tmp,
                   std::string& err);
// Deletes <root>/<slug>, drops its entries from the recent list and
// regenerates docs/index.md. Refuses anything that isn't an installed docset.
bool remove_docset(const std::string& root,
                   const std::string& slug,
                   std::string& err);
// Regenerates docs/index.md (and index.html while any docset has HTML).
bool write_level0(const std::string& root, std::string& err);

// ---- the docset registry ----------------------------------------------------

// Everything winter knows about, from docsets.inc: which docsets exist, where
// each comes from, and how a docset reads (fence, separator, filetypes).
enum class Origin {
    DevDocs,  // downloaded from DevDocs (index.json + db.json)
    MdTree,   // built from a markdown tree in a GitHub repo
    Javadoc,  // built from -javadoc.jar files: local caches, else Maven Central
    Unlisted,  // language info only; not offered for install
};
struct Docset {
    std::string_view slug, name, fence, sep, fts;
    Origin origin;
    // MdTree only
    std::string_view repo, ref, subdir, strip_ns, alias_from, alias_to;
    // Javadoc only: space-separated group:artifact list
    std::string_view coords;
};
const Docset* docset_for(std::string_view base);   // any row, nullptr: unknown
const Docset* source_for(std::string_view base);   // MdTree rows, else nullptr
const Docset* javadoc_for(std::string_view base);  // Javadoc rows, else nullptr

// Javadoc a build tool already fetched, for the coords of one Javadoc row.
struct JavadocSrc {
    std::string artifact;  // "lwjgl-opengl"
    std::string path;      // a -javadoc.jar, or an unpacked HTML directory
};
struct JavadocFind {
    std::string version;  // "3.3.6"; "system" for an unversioned distro tree
    std::vector<JavadocSrc> srcs;
};
// Looks in the Maven repository (settings.xml localRepository, else
// ~/.m2/repository, plus /usr/share/maven-repo), the Gradle cache
// ($GRADLE_USER_HOME or ~/.gradle) and /usr/share/javadoc. One entry per
// version that has at least one module, newest first, "system" last. A
// version missing some of the row's modules still counts; it just has fewer
// srcs.
std::vector<JavadocFind> find_javadoc(const Docset& s);
// The rows :DocInstall offers (not Unlisted), in registry order.
std::vector<const Docset*> all_docsets();
// Turns the extracted tree in `src_dir` into dl_dir/index.json + db.json, the
// same two files DevDocs provides, so build_docset needs no second path.
bool import_tree(const Docset& s,
                 const std::string& src_dir,
                 const std::string& dl_dir,
                 std::string& err);

// Turns the javadoc jars unpacked to src_dir/<artifact>/ into
// dl_dir/index.json + db.json (db values are HTML fragments, so build_docset
// converts them like DevDocs pages).
bool import_javadoc(const Docset& s,
                    const std::string& src_dir,
                    const std::string& dl_dir,
                    std::string& err);

// ---- the DevDocs catalogue
// ----------------------------------------------------

// Every docset DevDocs offers, as saved by the last install or update job
// (<root>/catalog.json, a copy of DevDocs' docs.json). Empty until then.
struct Avail {
    std::string slug, name, version;  // slug: "lua~5.4"; "go" for unversioned
};
std::vector<Avail> load_catalog(const std::string& root);

// ---- install job ------------------------------------------------------------

struct ScriptOpts {
    std::string exe;   // this binary: it converts (--doc-build)
    std::string slug;  // as typed; may be a base name like "lua"
    std::string root;  // ~/.local/share/winter/docs
    std::string cdn, manifest;
    bool html = false, update = false, force = false;
};
// The sh script the job runner executes for one docset: fetch with curl into
// a temporary directory beside the target, then hand over to --doc-build. For
// an MdTree docset it first resolves the branch head with git ls-remote, so
// :DocUpdate can tell whether anything changed and the download is pinned to
// the commit it checked.
// Every line it prints is a status-line progress message; a failure ends in
// a "FAILED: <step>" line and a non-zero exit.
std::string install_script(const ScriptOpts& o);
// A job that only refreshes <root>/catalog.json. Every install_script run
// refreshes it too; this is for :DocUpdate with nothing installed yet.
std::string catalog_script(const ScriptOpts& o);

// ---- cli ------------------------------------------------------------------

// --doc-resolve, --doc-check (its optional last argument is the upstream
// revision of an MdTree docset, or the version of a Javadoc one), --doc-locate
// (local Javadoc for a slug, see find_javadoc), --doc-build (reads
// WINTER_DOCS_REV). Returns the process exit code.
int cli_main(int argc, const char* argv[]);

}  // namespace doc
