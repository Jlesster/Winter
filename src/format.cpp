#include "format.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "bufedit.hpp"
#include "command.hpp"
#include "editor.hpp"
#include "format_run.hpp"
#include "json.hpp"
#include "loop.hpp"
#include "lsp_apply.hpp"
#include "lsp_client.hpp"
#include "lsp_sync.hpp"
#include "options.hpp"
#include "script_internal.hpp"
#include "syntax.hpp"

namespace fs = std::filesystem;

namespace {

struct Fmt {
    std::vector<std::string> cmd;
    std::string cwd = "$DIRNAME";
};
struct Rule {
    std::vector<std::string> names;  // empty = formatting disabled for ft
    bool first = false;              // stop_after_first: first usable wins
};

std::map<std::string, Fmt> g_fmt;
std::map<std::string, Rule> g_rules;
bool g_lsp_fallback = true;
std::string g_note;
bool g_busy = false;

using V = std::vector<std::string>;

void load_builtins() {
    g_fmt = {
        {"stylua",
         {V{"stylua", "--search-parent-directories", "--stdin-filepath",
            "$FILENAME", "-"}}},
        {"prettier", {V{"prettier", "--stdin-filepath", "$FILENAME"}}},
        {"prettierd", {V{"prettierd", "$FILENAME"}}},
        {"clang-format", {V{"clang-format", "--assume-filename=$FILENAME"}}},
        {"rustfmt", {V{"rustfmt", "--edition", "2021"}}},
        {"gofmt", {V{"gofmt"}}},
        {"goimports", {V{"goimports"}}},
        {"black", {V{"black", "-q", "-"}}},
        {"shfmt", {V{"shfmt", "-filename", "$FILENAME"}}},
        {"google-java-format", {V{"google-java-format", "-"}}},
    };
}

std::string sub_all(std::string s, const std::string& k, const std::string& v) {
    for(size_t p = 0; (p = s.find(k, p)) != std::string::npos; p += v.size())
        s.replace(p, k.size(), v);
    return s;
}

std::string expand(std::string s, const fs::path& file) {
    s = sub_all(s, "$FILENAME", file.string());
    s = sub_all(s, "$DIRNAME", file.parent_path().string());
    s = sub_all(s, "$BASENAME", file.filename().string());
    s = sub_all(s, "$EXT", file.extension().string());
    return s;
}

bool have_exe(const std::string& prog) {
    if(prog.find('/') != std::string::npos)
        return access(prog.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    if(!path) return false;
    const std::string p = path;
    for(size_t i = 0; i <= p.size();) {
        size_t e = p.find(':', i);
        if(e == std::string::npos) e = p.size();
        const std::string dir = p.substr(i, e - i);
        if(!dir.empty() && access((dir + "/" + prog).c_str(), X_OK) == 0)
            return true;
        i = e + 1;
    }
    return false;
}

bool lsp_can(Buffer& b) {
    const auto d = lsp_sync_doc(b);
    if(!d) return false;
    const Json& c = lsp_capabilities(d->server)["documentFormattingProvider"];
    return !c.is_null() && !(c.is_bool() && !c.as_bool());
}

bool usable(Buffer& b, const std::string& n) {
    if(n == "lsp") return lsp_can(b);
    const auto it = g_fmt.find(n);
    return it != g_fmt.end() && !it->second.cmd.empty() &&
           have_exe(it->second.cmd[0]);
}

struct Plan {
    std::vector<std::string> names;
    std::string label;  // what we resolved against, for messages
};

Plan make_plan(Buffer& b) {
    const std::string lang(Syntax::detect(b.filename));
    const std::string ext = fs::path(b.filename).extension().string();
    for(const std::string& key : {lang, ext}) {
        if(key.empty()) continue;
        const auto it = g_rules.find(key);
        if(it == g_rules.end()) continue;
        if(!it->second.first) return {it->second.names, key};
        for(const std::string& n : it->second.names)
            if(usable(b, n)) return {{n}, key};
        return {{}, key};
    }
    if(g_lsp_fallback && lsp_can(b)) return {{"lsp"}, lang};
    return {{}, lang.empty() ? ext : lang};
}

std::string serialize(const Buffer& b) {
    std::string s = b.pt.text();
    if(b.eol) s += '\n';
    return s;
}

std::string first_line(const std::string& s) {
    const size_t e = s.find('\n');
    return s.substr(0, e == std::string::npos ? s.size() : e);
}

// LSP formatting edits the buffer directly. "" = ok, else the reason.
std::string lsp_format(Buffer& b, int timeout_ms) {
    const auto doc = lsp_sync_doc(b, true);  // flush pending didChange first
    if(!doc) return "no language server attached";
    struct St {
        bool done = false;
        Json result, error;
    };
    const auto st = std::make_shared<St>();
    lsp_request(
        doc->server,
        "textDocument/formatting",
        Json::obj({{"textDocument", Json::obj({{"uri", doc->uri}})},
                   {"options",
                    Json::obj({{"tabSize", opts.shift_width},
                               {"insertSpaces", opts.expand_tab},
                               {"trimTrailingWhitespace", true},
                               {"insertFinalNewline", true}})}}),
        [st](const Json& r, const Json& e) {
            st->result = r;
            st->error = e;
            st->done = true;
        });
    if(!loop_pump_until([&] { return st->done; }, timeout_ms))
        return "language server timed out";
    if(!st->error.is_null())
        return std::string(st->error["message"].as_str("request failed"));
    if(!st->result.is_array() || st->result.size() == 0) return "";
    const Json we = Json::obj(
        {{"changes", Json::obj({{doc->uri, st->result}})}});
    const ApplyResult ar = lsp_apply_workspace_edit(doc->server, we);
    return ar.ok ? "" : ar.error;
}

struct Outcome {
    bool changed = false;
    std::string used;   // "stylua+lsp"
    std::string error;  // empty = ok
    bool ran = false;
};

Outcome run_plan(Buffer& b, const Plan& p) {
    Outcome o;
    const int to = opts.format_timeout;
    const uint64_t rev0 = b.pt.revision();
    const fs::path file = fs::absolute(b.filename);
    std::string cur = serialize(b);
    bool pending = false;  // `cur` holds output not yet in the buffer
    for(const std::string& n : p.names) {
        if(n == "lsp") {
            if(pending) {
                buffer_replace_text(b, cur, Origin::User);
                pending = false;
            }
            if(const std::string e = lsp_format(b, to); !e.empty()) {
                o.error = "lsp: " + e;
                break;
            }
            cur = serialize(b);
        } else {
            const auto it = g_fmt.find(n);
            if(it == g_fmt.end() || it->second.cmd.empty()) {
                o.error = "unknown formatter '" + n + "'";
                break;
            }
            std::vector<std::string> argv;
            for(const std::string& a : it->second.cmd)
                argv.push_back(expand(a, file));
            FmtOut r = run_formatter(
                argv, expand(it->second.cwd, file), cur, to);
            if(!r.ok) {
                const std::string why = r.code == 127 && !r.timeout
                    ? "not found"
                    : first_line(r.err).empty()
                        ? "exit " + std::to_string(r.code)
                        : first_line(r.err);
                o.error = n + ": " + why;
                break;
            }
            if(r.out.empty() && !cur.empty()) {
                o.error = n + ": empty output, buffer left alone";
                break;
            }
            cur = std::move(r.out);
            pending = true;
        }
        o.used += (o.used.empty() ? "" : "+") + n;
        o.ran = true;
    }
    if(pending && o.error.empty())
        buffer_replace_text(b, cur, Origin::User);
    o.changed = b.pt.revision() != rev0;
    return o;
}

}  // namespace

bool format_buffer(Buffer& b, bool verbose) {
    if(g_busy) return false;
    if(b.nofile || b.filename.empty() || b.pt.size() == 0) {
        if(verbose) editor_set_status_message("format: nothing to format");
        return false;
    }
    g_busy = true;
    const Plan p = make_plan(b);
    Outcome o;
    if(!p.names.empty()) o = run_plan(b, p);
    g_busy = false;

    if(!o.error.empty()) {
        g_note = ", format failed: " + o.error;
        if(verbose) editor_set_status_message("format: {}", o.error);
    } else if(verbose) {
        if(p.names.empty())
            editor_set_status_message(
                "format: no formatter for {} (winter.format.setup)",
                p.label.empty() ? "this file" : p.label);
        else if(o.changed)
            editor_set_status_message("formatted ({})", o.used);
        else editor_set_status_message("already formatted ({})", o.used);
    }
    return o.changed;
}

void format_on_save(Buffer& b) {
    g_note.clear();
    format_buffer(b, false);
}

std::string format_take_note() { return std::exchange(g_note, {}); }

// ---- lua: winter.format.setup{...} / winter.format.run()

namespace {

V lua_strings(lua_State* L, int t, const char* what) {
    V out;
    const auto n = static_cast<int>(lua_rawlen(L, t));
    for(int i = 1; i <= n; ++i) {
        lua_rawgeti(L, t, i);
        if(lua_type(L, -1) != LUA_TSTRING)
            luaL_error(L, "%s[%d] must be a string", what, i);
        out.emplace_back(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return out;
}

int l_setup(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "on_save");
    if(!lua_isnil(L, -1)) opts.format_on_save = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, 1, "lsp_fallback");
    if(!lua_isnil(L, -1)) g_lsp_fallback = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, 1, "timeout");
    if(lua_isnumber(L, -1))
        opts.format_timeout = static_cast<int>(
            std::clamp<lua_Integer>(lua_tointeger(L, -1), 50, 60000));
    lua_pop(L, 1);

    lua_getfield(L, 1, "formatters");  // name -> {cmd=, cwd=} (override/add)
    if(lua_istable(L, -1)) {
        const int t = lua_gettop(L);
        for(lua_pushnil(L); lua_next(L, t); lua_pop(L, 1)) {
            if(lua_type(L, -2) != LUA_TSTRING || !lua_istable(L, -1)) continue;
            const std::string name = lua_tostring(L, -2);
            Fmt f = g_fmt.contains(name) ? g_fmt[name] : Fmt{};
            lua_getfield(L, -1, "cmd");
            if(lua_istable(L, -1)) f.cmd = lua_strings(L, lua_gettop(L), "cmd");
            lua_pop(L, 1);
            lua_getfield(L, -1, "cwd");
            if(lua_type(L, -1) == LUA_TSTRING) f.cwd = lua_tostring(L, -1);
            lua_pop(L, 1);
            if(f.cmd.empty())
                luaL_error(L, "formatters.%s needs a cmd table", name.c_str());
            g_fmt[name] = std::move(f);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "by_ft");  // ft or ".ext" -> {names..., stop_after_first}
    if(lua_istable(L, -1)) {
        const int t = lua_gettop(L);
        for(lua_pushnil(L); lua_next(L, t); lua_pop(L, 1)) {
            if(lua_type(L, -2) != LUA_TSTRING) continue;
            const std::string ft = lua_tostring(L, -2);
            Rule r;
            if(lua_istable(L, -1)) {
                r.names = lua_strings(L, lua_gettop(L), ft.c_str());
                lua_getfield(L, -1, "stop_after_first");
                r.first = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
            }  // `false` / non-table = disabled for this filetype
            g_rules[ft] = std::move(r);
        }
    }
    lua_pop(L, 1);
    return 0;
}

int l_run(lua_State*) {
    format_buffer(E.buf(), true);
    return 0;
}

const luaL_Reg kFormat[] = {
    {"setup", l_setup}, {"run", l_run}, {nullptr, nullptr}};

}  // namespace

void format_init() {
    load_builtins();
    command_add(
        {.name = "format",
         .aliases = {"fmt"},
         .desc = "Format the buffer; :format on|off|toggle sets format-on-save",
         .usage = ":format [on|off|toggle]",
         .nargs = Nargs::Opt,
         .run = [](const CmdLine& c) {
             if(c.argv.empty()) {
                 format_buffer(E.buf(), true);
                 return;
             }
             const std::string& a = c.argv[0];
             if(a == "on") opts.format_on_save = true;
             else if(a == "off") opts.format_on_save = false;
             else if(a == "toggle") opts.format_on_save = !opts.format_on_save;
             else {
                 editor_set_status_message(":format [on|off|toggle]");
                 return;
             }
             editor_set_status_message("format on save: {}",
                                       opts.format_on_save ? "on" : "off");
         }});
    if(script_state()) script_register("format", kFormat);
}
