#include "lsp_nav.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <tuple>
#include <vector>
#include "buflist.hpp"
#include "command.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "jumplist.hpp"
#include "json.hpp"
#include "lsp_client.hpp"
#include "lsp_sync.hpp"
#include "motion.hpp"
#include "picker.hpp"
#include "piece.hpp"
#include "utf8.hpp"

namespace fs = std::filesystem;

namespace {

struct Loc {
    std::string path;
    size_t line = 0;
    size_t col = 0;  

    auto key() const { return std::tie(path, line, col); }
    bool operator==(const Loc& o) const { return key() == o.key(); }
};

struct KindInfo {
    const char* name;
    const char* method;
    const char* provider;
};

constexpr KindInfo kKinds[] = {
    {"definition", "textDocument/definition", "definitionProvider"},
    {"declaration", "textDocument/declaration", "declarationProvider"},
    {"type definition", "textDocument/typeDefinition", "typeDefinitionProvider"},
    {"implementation", "textDocument/implementation", "implementationProvider"},
    {"references", "textDocument/references", "referencesProvider"},
};

const KindInfo& info(NavKind k) { return kKinds[static_cast<size_t>(k)]; }

bool supported(const Json& v) {
    return !v.is_null() && !(v.is_bool() && !v.as_bool());
}

std::vector<Loc> parse_locations(const Json& r) {
    std::vector<Loc> out;
    const auto one = [&](const Json& j) {
        const bool link = j.has("targetUri");
        const std::string_view uri = (link ? j["targetUri"] : j["uri"]).as_str();
        const Json& rng = link ? (j.has("targetSelectionRange")
                                  ? j["targetSelectionRange"]
                                      : j["targetRange"])
            : j["range"];
            if(uri.empty()) return;
            out.push_back({lsp_path(uri),
                static_cast<size_t>(rng["start"]["line"].as_int()),
                static_cast<size_t>(rng["start"]["character"].as_int())});
    };
    if(r.is_array())
        for(const Json& j : r.items()) one(j);
    else if(r.is_object())
        one(r);
    return out;
}

bool is_here(const Loc& l) {
    const Buffer& b = E.buf();
    if(b.filename.empty()) return false;
    std::error_code ec;
    return b.pt.line_of(cursor_offset()) == l.line &&
        fs::equivalent(b.filename, l.path, ec) && !ec;
}

void jump_to(const Loc& l, bool utf16) {
    jump_push();
    if(!jump_open(l.path)) {
        editor_set_status_message("lsp: cannot open {}", l.path);
        return;
    }
    const PieceTable& pt = E.buf().pt;
    const size_t line = std::min(l.line, pt.line_count() - 1);
    const size_t off = lsp_offset(pt, static_cast<int64_t>(line),
                                  static_cast<int64_t>(l.col), utf16);
    set_cursor_offset(motion::clamp_normal(pt, off));
}

struct LineCache {
    std::map<std::string, std::vector<std::string>> files;

    std::string_view get(const std::string& path, size_t line) {
        auto it = files.find(path);
        if(it == files.end()) {
            std::vector<std::string> v;
            std::error_code ec;
            const auto size = fs::file_size(path, ec);
            if(!ec && size <= (4u << 20)) {
                std::ifstream in(path);
                for(std::string l; std::getline(in, l);) v.push_back(std::move(l));
            }
            it = files.emplace(path, std::move(v)).first;
        }
        if(line >= it->second.size()) return {};
        return it->second[line];
    }
};

size_t byte_col(std::string_view text, size_t ch, bool utf16) {
    if(!utf16) return std::min(ch, text.size());
    size_t i = 0, units = 0;
    while(i < text.size() && units < ch) {
        const Decoded d = utf8_decode(text, i);
        units += d.cp >= 0x10000 ? 2 : 1;
        i += d.len;
    }
    return i;
}

void show_picker(std::vector<Loc> locs, bool utf16, const char* what) {
    std::ranges::sort(locs, [](const Loc& a, const Loc& b) { return a.key() < b.key(); });
    locs.erase(std::unique(locs.begin(), locs.end()), locs.end());

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    LineCache cache;
    std::vector<PickItem> items;
    items.reserve(locs.size());
    for(size_t i = 0; i < locs.size(); ++i) {
        const Loc& l = locs[i];
        const std::string_view src = cache.get(l.path, l.line);
        std::string_view shown = src;
        while(!shown.empty() && (shown.front() == ' ' || shown.front() == '\t'))
            shown.remove_prefix(1);

        const fs::path rel = fs::path(l.path).lexically_relative(cwd);
        const std::string name =
            (rel.empty() || rel.native().starts_with("..")) ? l.path : rel.string();

            PickItem it;
            it.id = i;
            it.path = l.path;
            it.line = static_cast<int>(l.line) + 1;
            it.col = static_cast<int>(byte_col(src, l.col, utf16)) + 1;
            it.text = std::format("{}:{}  {}", name, it.line, shown);
            items.push_back(std::move(it));
    }

    PickerSpec s;
    s.prompt = what;
    s.fuzzy = true;
    s.preview = true;
    s.on_accept = [](const PickItem& it) {
        jump_push();
        if(!jump_goto(it.path, static_cast<size_t>(it.line - 1),
                      static_cast<size_t>(it.col - 1)))
            editor_set_status_message("lsp: cannot open {}", it.path);
    };
    picker_open(std::move(s));
    picker_add(std::move(items));
}

bool has_ext(const std::string& ext, std::initializer_list<const char*> list) {
    return std::ranges::any_of(list, [&](const char* e) { return ext == e; });
}

void swap_by_name(const std::string& path) {
    static constexpr std::initializer_list<const char*> hdr = {".h", ".hh", ".hpp", ".hxx", ".h++", ".inl"};
    static constexpr std::initializer_list<const char*> src = {".c", ".cc", ".cpp", ".cxx", ".c++", ".m", ".mm"};

    const fs::path p(path);
    const std::string ext = p.extension().string();
    const bool is_hdr = has_ext(ext, hdr);
    if(!is_hdr && !has_ext(ext, src)) {
        editor_set_status_message("swap: {} is not a C/C++ source or header",
                                  p.filename().string());
        return;
    }
    const auto& want = is_hdr ? src : hdr;

    std::vector<fs::path> dirs = {p.parent_path()};
    const std::string d = p.parent_path().string();
    static constexpr std::pair<const char*, const char*> kSwaps[] = {
        {"/include", "/src"}, {"/src", "/include"},
        {"/inc", "/src"},     {"/src", "/inc"},
        {"/include", "/source"}, {"/source", "/include"},
    };
    for(const auto& [a, b] : kSwaps)
        if(const size_t i = d.rfind(a); i != std::string::npos)
            dirs.emplace_back(d.substr(0, i) + b + d.substr(i + std::strlen(a)));

    for(const fs::path& dir : dirs)
        for(const char* e : want) {
            const fs::path c = dir / (p.stem().string() + e);
            std::error_code ec;
            if(fs::is_regular_file(c, ec)) {
                jump_push();
                buffer_open(c.string());
                return;
            }
        }
    editor_set_status_message("swap: no counterpart found for {}", p.filename().string());
}

}  // namespace

bool nav_kind_from_name(std::string_view name, NavKind& out) {
    if(name == "definition") out = NavKind::Definition;
    else if(name == "declaration") out = NavKind::Declaration;
    else if(name == "type_definition") out = NavKind::TypeDefinition;
    else if(name == "implementation") out = NavKind::Implementation;
    else if(name == "references") out = NavKind::References;
    else return false;
    return true;
}

void lsp_goto(NavKind kind) {
    const KindInfo& ki = info(kind);
    Buffer& b = E.buf();
    const auto doc = lsp_sync_doc(b, true);
    if(!doc) {
        editor_set_status_message("lsp: no language server for this buffer");
        return;
    }
    if(!supported(lsp_capabilities(doc->server)[ki.provider])) {
        editor_set_status_message("lsp: server does not support {}", ki.name);
        return;
    }

    const size_t cur = cursor_offset();
    Json params = Json::obj(
        {{"textDocument", Json::obj({{"uri", doc->uri}})},
            {"position",
                Json::obj({{"line", b.pt.line_of(cur)},
                    {"character", lsp_col(b.pt, cur, doc->utf16)}})}});
    if(kind == NavKind::References)
        params.set("context", Json::obj({{"includeDeclaration", true}}));

    lsp_request(
        doc->server, ki.method, std::move(params),
        [buf = &b, rev = b.pt.revision(), utf16 = doc->utf16, kind](
            const Json& result, const Json& error) {
            if(buf != &E.buf() || buf->pt.revision() != rev) return;
            const KindInfo& k = info(kind);
            if(!error.is_null()) {
                editor_set_status_message(
                    "lsp: {}", std::string(error["message"].as_str("request failed")));
                return;
            }
            std::vector<Loc> locs = parse_locations(result);
            if(locs.empty()) {
                editor_set_status_message("lsp: no {} found", k.name);
                return;
            }
            if(locs.size() == 1) {
                if(!is_here(locs[0])) jump_to(locs[0], utf16);
                else if(kind == NavKind::References)
                    editor_set_status_message("lsp: no other references");
                else
                    editor_set_status_message("lsp: already at the {}", k.name);
                return;
            }
            show_picker(std::move(locs), utf16, k.name);
        });
}

void lsp_switch_source_header() {
    Buffer& b = E.buf();
    const auto doc = lsp_sync_doc(b, true);
    if(doc && doc->server_name == "clangd") {
        lsp_request(doc->server, "textDocument/switchSourceHeader",
                    Json::obj({{"uri", doc->uri}}),
                    [buf = &b, path = b.filename](const Json& r, const Json&) {
                        if(buf != &E.buf()) return;
                        if(r.is_string() && !r.as_str().empty()) {
                            jump_push();
                            buffer_open(lsp_path(r.as_str()));
                        } else {
                            swap_by_name(path);
                        }
                    });
        return;
    }
    if(b.filename.empty()) {
        editor_set_status_message("swap: buffer has no file name");
        return;
    }
    swap_by_name(b.filename);
}

void lsp_nav_register_commands() {
    command_add({.name = "A",
        .desc = "Switch between header and implementation file",
            .nargs = Nargs::None,
            .run = [](const CmdLine&) { lsp_switch_source_header(); }});
}

void lsp_show_location(const std::string &path, size_t line, size_t ch, bool utf16) {
    jump_to({path, line, ch}, utf16);
}
