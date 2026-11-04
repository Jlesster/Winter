#include "lsp_apply.hpp"
#include <algorithm>
#include <filesystem>
#include <format>
#include <vector>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include "buflist.hpp"
#include "lsp_sync.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "motion.hpp"
#include "piece.hpp"

namespace fs = std::filesystem;

namespace {

struct Change {
    size_t start = 0, end = 0;
    std::string text;
};

struct FileEdits {
    std::string path;
    std::vector<const Json*> lists;
    Buffer* buf = nullptr;
    std::vector<Change> changes;
};

struct Step {
    enum Kind { Edit, Create, Rename, Delete } kind = Edit;
    FileEdits file;
    std::string from, to;
    int64_t version = -1;
    bool overwrite = false, ignore = false, recursive = false;
};

fs::path norm(std::string p) {
    fs::path q = fs::path(std::move(p)).lexically_normal();
    if(!q.has_filename()) q = q.parent_path();
    return q;
}

bool collect(const Json& we, std::vector<Step>& steps, std::string& err) {
    if(we["documentChanges"].is_array()) {
        for(const Json& dc : we["documentChanges"].items()) {
            Step s;
            const std::string_view kind = dc["kind"].as_str();
            const Json& o = dc["options"];
            if(kind == "create") {
                s.kind = Step::Create;
                s.from = lsp_path(dc["uri"].as_str());
                s.overwrite = o["overwrite"].as_bool();
                s.ignore = o["ignoreIfExists"].as_bool();
            } else if(kind == "rename") {
                s.kind = Step::Rename;
                s.from = lsp_path(dc["oldUri"].as_str());
                s.to = lsp_path(dc["newUri"].as_str());
                s.overwrite = o["overwrite"].as_bool();
                s.ignore = o["ignoreIfExists"].as_bool();
            } else if(kind == "delete") {
                s.kind = Step::Delete;
                s.from = lsp_path(dc["uri"].as_str());
                s.recursive = o["recursive"].as_bool();
                s.ignore = o["ignoreIfNotExists"].as_bool();
            } else if(kind.empty()) {
                s.file.path = lsp_path(dc["textDocument"]["uri"].as_str());
                s.file.lists.push_back(&dc["edits"]);
                if(!dc["textDocument"]["version"].is_null())
                    s.version = dc["textDocument"]["version"].as_int();
            } else {
                err = std::format("unknown change kind '{}'", kind);
                return false;
            }
            steps.push_back(std::move(s));
        }
    } else if(we["changes"].is_object()) {
        for(const auto& [uri, list] : we["changes"].members()) {
            Step s;
            s.file.path = lsp_path(uri);
            s.file.lists.push_back(&list);
            steps.push_back(std::move(s));
        }
    }
    return true;
}

void retarget_buffers(const fs::path& from, const fs::path& to) {
    for(auto& up : E.buffers) {
        Buffer& b = *up;
        if(b.filename.empty()) continue;
        const fs::path rel =
            norm(fs::absolute(b.filename).string()).lexically_relative(from);
            if(rel.empty() || *rel.begin() == "..") continue;
            lsp_sync_detach(b);
            b.filename = (rel == "." ? to : to / rel).string();
            lsp_sync_attach(b);
    }
}

void close_under(const fs::path& root) {
    for(size_t i = E.buffers.size(); i-- > 0;) {
        const Buffer& b = *E.buffers[i];
        if(b.filename.empty()) continue;
        const fs::path rel =
            norm(fs::absolute(b.filename).string()).lexically_relative(root);
            if(!rel.empty() && *rel.begin() != "..") buffer_close(i, true);
    }
}

bool do_create(const Step& s, std::string& err) {
    std::error_code ec;
    if(fs::exists(s.from, ec)) {
        if(s.ignore && !s.overwrite) return true;
        if(!s.overwrite) {
            err = s.from + " already exists";
            return false;
        }
    }
    fs::create_directories(fs::path(s.from).parent_path(), ec);
    std::ofstream out(s.from, std::ios::trunc);
    if(!out) {
        err = "cannot create " + s.from;
        return false;
    }
    return true;
}

bool do_rename(const Step& s, std::string& err) {
    std::error_code ec;
    if(fs::exists(s.to, ec)) {
        if(s.ignore && !s.overwrite) return true;
        if(!s.overwrite) {
            err = s.to + " already exists";
            return false;
        }
        close_under(norm(s.to));
        fs::remove_all(s.to, ec);
    }
    fs::create_directories(fs::path(s.to).parent_path(), ec);
    fs::rename(s.from, s.to, ec);
    if(ec) {
        err = std::format("rename failed: {}", ec.message());
        return false;
    }
    retarget_buffers(norm(s.from), norm(s.to));
    return true;
}

bool do_delete(const Step& s, std::string& err) {
    std::error_code ec;
    if(!fs::exists(s.from, ec)) {
        if(s.ignore) return true;
        err = s.from + " does not exist";
        return false;
    }
    close_under(norm(s.from));
    if(s.recursive) fs::remove_all(s.from, ec);
    else fs::remove(s.from, ec);
    if(ec) {
        err = std::format("delete failed: {}", ec.message());
        return false;
    }
    return true;
}

bool convert(FileEdits& f, bool utf16, std::string& err) {
    const PieceTable& pt = f.buf->pt;
    for(const Json* list : f.lists) {
        if(!list->is_array()) continue;
        for(const Json& e : list->items()) {
            const Json& r = e["range"];
            Change c;
            c.start = lsp_offset(pt,
                                 r["start"]["line"].as_int(),
                                 r["start"]["character"].as_int(),
                                 utf16);
            c.end = lsp_offset(pt,
                               r["end"]["line"].as_int(),
                               r["end"]["character"].as_int(),
                               utf16);
            if(c.end < c.start) {
                err = std::format("{}: edit range is reversed", f.path);
                return false;
            }
            c.text = std::string(e["newText"].as_str());
            f.changes.push_back(std::move(c));
        }
    }
    std::ranges::stable_sort(f.changes, {}, &Change::start);
    for(size_t i = 1; i < f.changes.size(); ++i)
        if(f.changes[i - 1].end > f.changes[i].start) {
            err = std::format("{}: overlapping edits", f.path);
            return false;
        }
    return true;
}

bool apply_edits(FileEdits& f, ApplyResult& res) {
    if(f.changes.empty()) return true;
    PieceTable& pt = f.buf->pt;
    const bool current = f.buf == &E.buf();
    const size_t cur = current ? cursor_offset() : 0;

    size_t new_cur = cur;
    std::ptrdiff_t delta = 0;
    bool inside = false;
    for(const Change& c : f.changes) {
        const std::ptrdiff_t d = static_cast<std::ptrdiff_t>(c.text.size()) -
            static_cast<std::ptrdiff_t>(c.end - c.start);
            if(c.end <= cur) delta += d;
            else if(c.start < cur) {
                new_cur =
                    static_cast<size_t>(static_cast<std::ptrdiff_t>(c.start) + delta);
                    inside = true;
                    break;
            }
    }
    if(!inside)
        new_cur = static_cast<size_t>(static_cast<std::ptrdiff_t>(cur) + delta);

    const size_t first = f.changes.front().start;
    {
        PieceTable::Group g(pt);
        for(auto it = f.changes.rbegin(); it != f.changes.rend(); ++it) {
            if(it->end > it->start) pt.remove(it->start, it->end - it->start);
            if(!it->text.empty()) pt.insert(it->start, it->text);
        }
    }
    if(current) {
        set_cursor_offset(motion::clamp_normal(pt, new_cur));
    } else {
        const size_t at = std::min(first, pt.size());
        const size_t line = pt.line_of(at);
        f.buf->cy = static_cast<int>(line);
        f.buf->cx = static_cast<int>(at - pt.line_start(line));
    }
    ++res.files;
    res.edits += f.changes.size();
    return true;
}

}  // namespace

ApplyResult lsp_apply_workspace_edit(LspId server, const Json& edit) {
    ApplyResult res;
    std::vector<Step> steps;
    std::string err;
    if(!collect(edit, steps, err)) return {false, err, 0, 0};

    std::vector<std::string> made;
    for(Step& s : steps) {
        if(s.kind == Step::Create) made.push_back(s.from);
        else if(s.kind == Step::Rename) made.push_back(s.to);
        if(s.kind != Step::Edit) continue;
        if(std::ranges::find(made, s.file.path) != made.end()) continue;
        Buffer* b = buffer_get(s.file.path);
        if(!b)
            return {false, std::format("cannot open {}", s.file.path), 0, 0};
        if(s.version >= 0) {
            const auto v = lsp_sync_version(*b);
            if(v && *v != s.version)
                return {false,
                    s.file.path + ": changed since the server computed this edit",
                    0,
                    0};
        }
    }

    const bool utf16 = lsp_encoding(server) != "utf-8";
    for(Step& s : steps) {
        bool ok = true;
        switch(s.kind) {
        case Step::Create: ok = do_create(s, err); break;
        case Step::Rename: ok = do_rename(s, err); break;
        case Step::Delete: ok = do_delete(s, err); break;
        case Step::Edit:
            s.file.buf = buffer_get(s.file.path);
            if(!s.file.buf) {
                err = "cannot open " + s.file.path;
                ok = false;
            } else {
                ok = convert(s.file, utf16, err) &&
                    apply_edits(s.file, res);
            }
            break;
        }
        if(!ok) {
            res.ok = false;
            res.error = (res.edits ? "partial: " : "") + err;
            return res;
        }
    }
    return res;
}
