#include "lsp_rename.hpp"
#include <cctype>
#include <string>
#include "command.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "inputbox.hpp"
#include "json.hpp"
#include "lsp_apply.hpp"
#include "lsp_client.hpp"
#include "lsp_sync.hpp"
#include "piece.hpp"

namespace {

struct Ctx {
    Buffer* buf;
    uint64_t rev;
    LspDoc doc;
    size_t at;
};

bool stale(const Ctx& c) {
    if(c.buf == &E.buf() && c.buf->pt.revision() == c.rev) return false;
    editor_set_status_message("rename: buffer changed, ask again");
    return true;
}

Json position(const Ctx& c) {
    const PieceTable& pt = c.buf->pt;
    return Json::obj({{"line", pt.line_of(c.at)},
                      {"character", lsp_col(pt, c.at, c.doc.utf16)}});
}

std::string word_under(const PieceTable& pt, size_t at) {
    const size_t line = pt.line_of(at);
    const size_t ls = pt.line_start(line);
    const std::string text = pt.text(ls, pt.line_end(line) - ls);
    const auto w = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch >= 0x80;
    };
    size_t s = at - ls, e = s;
    while(s > 0 && w(text[s - 1])) --s;
    while(e < text.size() && w(text[e])) ++e;
    return text.substr(s, e - s);
}

void submit(const Ctx& c, std::string name) {
    if(name.empty() || stale(c)) return;
    Json p = Json::obj({{"textDocument", Json::obj({{"uri", c.doc.uri}})},
                        {"position", position(c)},
                        {"newName", name}});
    lsp_request(
        c.doc.server,
        "textDocument/rename",
        std::move(p),
        [c](const Json& r, const Json& err) {
            if(!err.is_null()) {
                editor_set_status_message(
                    "rename: {}", std::string(err["message"].as_str("failed")));
                return;
            }
            if(!r.is_object()) {
                editor_set_status_message("rename: no changes");
                return;
            }
            if(stale(c)) return;
            const ApplyResult a = lsp_apply_workspace_edit(c.doc.server, r);
            if(!a.ok) editor_set_status_message("rename failed: {}", a.error);
            else
                editor_set_status_message(
                    "renamed: {} edit(s) in {} file(s)", a.edits, a.files);
        });
}

void prompt(const Ctx& c, std::string initial) {
    InputSpec s;
    s.prompt = "rename: ";
    s.initial = std::move(initial);
    s.on_submit = [c](std::string n) { submit(c, std::move(n)); };
    input_open(std::move(s));
}

}  // namespace

void lsp_rename(std::string_view preset) {
    Buffer& b = E.buf();
    const auto doc = lsp_sync_doc(b, true);
    if(!doc) {
        editor_set_status_message("lsp: no language server for this buffer");
        return;
    }
    const Json& prov = lsp_capabilities(doc->server)["renameProvider"];
    if(prov.is_null() || (prov.is_bool() && !prov.as_bool())) {
        editor_set_status_message("lsp: server does not support rename");
        return;
    }
    const Ctx c{&b, b.pt.revision(), *doc, cursor_offset()};
    if(!preset.empty()) {
        submit(c, std::string(preset));
        return;
    }
    if(!prov["prepareProvider"].as_bool()) {
        prompt(c, word_under(b.pt, c.at));
        return;
    }
    Json p = Json::obj({{"textDocument", Json::obj({{"uri", doc->uri}})},
                        {"position", position(c)}});
    lsp_request(
        doc->server,
        "textDocument/prepareRename",
        std::move(p),
        [c](const Json& r, const Json& err) {
            if(!err.is_null()) {
                editor_set_status_message(
                    "rename: {}",
                    std::string(err["message"].as_str("cannot rename here")));
                return;
            }
            if(r.is_null()) {
                editor_set_status_message("rename: nothing renameable here");
                return;
            }
            if(stale(c)) return;
            const PieceTable& pt = c.buf->pt;
            std::string init;
            const Json& rg = r.has("range") ? r["range"] : r;
            if(r.has("placeholder")) {
                init = std::string(r["placeholder"].as_str());
            } else if(rg.has("start")) {
                const size_t s = lsp_offset(pt,
                                            rg["start"]["line"].as_int(),
                                            rg["start"]["character"].as_int(),
                                            c.doc.utf16);
                const size_t e = lsp_offset(pt,
                                            rg["end"]["line"].as_int(),
                                            rg["end"]["character"].as_int(),
                                            c.doc.utf16);
                if(e >= s) init = pt.text(s, e - s);
            } else {
                init = word_under(pt, c.at);
            }
            prompt(c, std::move(init));
        });
}

void lsp_rename_register() {
    command_add({.name = "Rename",
                 .desc = "LSP rename symbol",
                 .nargs = Nargs::Opt,
                 .run = [](const CmdLine& cl) { lsp_rename(cl.args); }});
}
