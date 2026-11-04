#include "lsp_actions.hpp"
#include <algorithm>
#include <format>
#include <memory>
#include <vector>
#include "command.hpp"
#include "diag.hpp"
#include "editor.hpp"
#include "json.hpp"
#include "lsp_apply.hpp"
#include "buflist.hpp"
#include "lsp_nav.hpp"
#include "lsp_client.hpp"
#include "lsp_sync.hpp"
#include "picker.hpp"
#include "piece.hpp"
#include "utf8.hpp"

namespace {

bool supported(const Json& v) {
    return !v.is_null() && !(v.is_bool() && !v.as_bool());
}

Json pos_json(const PieceTable& pt, size_t off, bool utf16) {
    return Json::obj(
        {{"line", pt.line_of(off)}, {"character", lsp_col(pt, off, utf16)}});
}

Json range_json(const PieceTable& pt, size_t s, size_t e, bool utf16) {
    return Json::obj(
        {{"start", pos_json(pt, s, utf16)}, {"end", pos_json(pt, e, utf16)}});
}

void request_range(const PieceTable& pt, size_t& s, size_t& e) {
    s = e = cursor_offset();
    if(E.mode != VISUAL) return;
    s = std::min(E.v_anchor, e);
    e = std::max(E.v_anchor, e);
    if(E.v_line) {
        s = pt.line_start(pt.line_of(s));
        e = pt.line_end(pt.line_of(e));
    } else if(e < pt.size()) {
        e += utf8_decode(pt.text(e, 4), 0).len;
    }
}

struct ActionCtx {
    Buffer* buf;
    uint64_t rev;
    LspId server;
};

bool stale(const ActionCtx& c) {
    if(c.buf == &E.buf() && c.buf->pt.revision() == c.rev) return false;
    editor_set_status_message("code action: buffer changed, ask again");
    return true;
}

void execute(LspId server, const Json& command) {
    Json p = Json::obj({{"command", std::string(command["command"].as_str())}});
    if(command["arguments"].is_array())
        p.set("arguments", command["arguments"]);
    lsp_request(
        server,
        "workspace/executeCommand",
        std::move(p),
        [](const Json&, const Json& err) {
            if(!err.is_null())
                editor_set_status_message(
                    "lsp: {}",
                    std::string(err["message"].as_str("command failed")));
        });
}

void finish(const ActionCtx& c, const Json& action) {
    if(action.has("edit")) {
        const ApplyResult r =
            lsp_apply_workspace_edit(c.server, action["edit"]);
        if(!r.ok) {
            editor_set_status_message("code action failed: {}", r.error);
            return;
        }
        editor_set_status_message(
            "code action: {} edit(s) in {} file(s)", r.edits, r.files);
    }
    if(action["command"].is_object()) execute(c.server, action["command"]);
}

void run(const ActionCtx& c, const Json& action) {
    if(stale(c)) return;
    if(action["command"].is_string()) {
        execute(c.server, action);
        return;
    }
    const bool lazy =
        !action.has("edit") &&
        lsp_capabilities(c.server)["codeActionProvider"]["resolveProvider"]
            .as_bool();
    if(!lazy) {
        finish(c, action);
        return;
    }
    lsp_request(
        c.server,
        "codeAction/resolve",
        action,
        [c](const Json& r, const Json& err) {
            if(stale(c)) return;
            if(!err.is_null() || !r.is_object()) {
                editor_set_status_message("code action: resolve failed");
                return;
            }
            finish(c, r);
        });
}

void show_menu(const ActionCtx& c, const Json& result) {
    auto items = std::make_shared<std::vector<Json>>();
    if(result.is_array())
        for(const Json& a : result.items())
            if(!a.has("disabled")) items->push_back(a);
    if(items->empty()) {
        editor_set_status_message("lsp: no code actions here");
        return;
    }
    std::ranges::stable_partition(
        *items, [](const Json& a) { return a["isPreferred"].as_bool(); });

    std::vector<PickItem> rows;
    rows.reserve(items->size());
    for(size_t i = 0; i < items->size(); ++i) {
        const Json& a = (*items)[i];
        PickItem it;
        it.id = i;
        it.text = std::string(a["title"].as_str("(untitled)"));
        it.detail = std::string(
            a["kind"].as_str(a["command"].is_string() ? "command" : ""));
        rows.push_back(std::move(it));
    }

    PickerSpec s;
    s.prompt = "code action";
    s.fuzzy = true;
    s.preview = false;
    s.on_accept = [c, items](const PickItem& it) {
        if(it.id < items->size()) run(c, (*items)[it.id]);
    };
    picker_open(std::move(s));
    picker_add(std::move(rows));
}

}  // namespace

void lsp_code_action() {
    Buffer& b = E.buf();
    const auto doc = lsp_sync_doc(b, true);
    if(!doc) {
        editor_set_status_message("lsp: no language server for this buffer");
        return;
    }
    if(!supported(lsp_capabilities(doc->server)["codeActionProvider"])) {
        editor_set_status_message("lsp: server does not offer code actions");
        return;
    }

    const PieceTable& pt = b.pt;
    size_t s = 0, e = 0;
    request_range(pt, s, e);
    if(E.mode == VISUAL) set_mode(EditorMode::NORMAL);

    Json diags = Json::arr();
    const size_t ls = pt.line_start(pt.line_of(s));
    const size_t le = pt.line_end(pt.line_of(e));
    for(const Diag& d : b.diags) {
        if(d.end < ls || d.start > le || !d.raw.is_object()) continue;
        Json j = d.raw;
        j.set("range", range_json(pt, d.start, d.end, doc->utf16));
        diags.push(std::move(j));
    }

    Json params = Json::obj(
        {{"textDocument", Json::obj({{"uri", doc->uri}})},
         {"range", range_json(pt, s, e, doc->utf16)},
         {"context", Json::obj({{"diagnostics", std::move(diags)}})}});

    const ActionCtx c{&b, pt.revision(), doc->server};
    lsp_request(
        doc->server,
        "textDocument/codeAction",
        std::move(params),
        [c](const Json& result, const Json& error) {
            if(c.buf != &E.buf() || c.buf->pt.revision() != c.rev) return;
            if(!error.is_null()) {
                editor_set_status_message(
                    "lsp: {}",
                    std::string(error["message"].as_str("request failed")));
                return;
            }
            show_menu(c, result);
        });
}

void lsp_actions_register() {
    lsp_on_request("workspace/applyEdit", [](LspId id, const Json& p) {
        const ApplyResult r = lsp_apply_workspace_edit(id, p["edit"]);
        Json out = Json::obj({{"applied", r.ok}});
        if(!r.ok) out.set("failureReason", r.error);
        else
            editor_set_status_message(
                "lsp: applied {} edit(s) in {} file(s)", r.edits, r.files);
        return out;
    });
    lsp_on("window/showMessage", [](LspId, const Json& p) {
        editor_set_status_message("lsp: {}", std::string(p["message"].as_str()));
    });
    lsp_on("window/logMessage", [](LspId, const Json&) {});

    lsp_on_request("window/showDocument", [](LspId id, const Json& p) {
        if(p["external"].as_bool()) return Json::obj({{"success", false}});
        const std::string path = lsp_path(p["uri"].as_str());
        const bool focus = p.has("takeFocus") ? p["takeFocus"].as_bool() : true;
        if(!focus) return Json::obj({{"success", buffer_get(path) != nullptr}});
        const Json& st = p["selection"]["start"];
        lsp_show_location(path,
                          static_cast<size_t>(st["line"].as_int()),
                          static_cast<size_t>(st["character"].as_int()),
                          lsp_encoding(id) != "utf-8");
        return Json::obj({{"success", true}});
    });

    lsp_on_request_async(
        "window/showMessageRequest",
        [](LspId, const Json& p, LspRespond done) {
            const Json& acts = p["actions"];
            if(!acts.is_array() || acts.items().empty()) {
                done(Json());
                return;
            }
            auto titles = std::make_shared<std::vector<std::string>>();
            auto answered = std::make_shared<bool>(false);
            std::vector<PickItem> rows;
            for(const Json& a : acts.items()) {
                rows.push_back(
                    {.text = std::string(a["title"].as_str()),
                        .id = titles->size()});
                titles->push_back(rows.back().text);
            }
            PickerSpec s;
            s.prompt = std::string(p["message"].as_str());
            s.fuzzy = false;
            s.on_accept = [=](const PickItem& it) {
                *answered = true;
                done(Json::obj({{"title", (*titles)[it.id]}}));
            };
            s.on_close = [=] {
                if(!*answered) done(Json());
            };
            picker_open(std::move(s));
            picker_add(std::move(rows));
        });
    command_add({.name = "CodeAction",
                 .desc = "Code actions at the cursor / selection",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { lsp_code_action(); }});
}
