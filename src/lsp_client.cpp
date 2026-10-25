#include "lsp_client.hpp"
#include <unistd.h>
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include "job.hpp"
#include "loop.hpp"
#include "lsp_reader.hpp"

namespace {
enum class State { Starting, Ready, Dead };

struct Client {
    LspId id = 0;
    LspSpec spec;
    JobId job = 0;
    State state = State::Starting;
    LspReader reader;
    int next_req = 1;
    std::unordered_map<int, LspReply> pending;
    std::vector<std::string> queued;
    Json caps;
    std::string encoding = "utf-16";
    std::string err_tail;
    std::string fail_reason;
    bool stopping = false;
};

std::vector<std::unique_ptr<Client>> g_clients;
LspId g_next_id = 0;
std::map<std::string, LspNotify, std::less<>> g_notify;
const Json kNone;

Client* get(LspId id) {
    for(auto& c : g_clients)
        if(c->id == id) return c.get();
    return nullptr;
}

std::string sh_quote(std::string_view s) {
    std::string o = "'";
    for(const char ch : s) {
        if(ch == '\'') o += "'\\''";
        else o += ch;
    }
    o += '\'';
    return o;
}

std::string frame(const Json& msg) {
    const std::string body = msg.dump();
    std::string out = std::format("Content-Length: {}\r\n\r\n", body.size());
    out += body;
    return out;
}

void send(Client& c, const Json& msg, bool force = false) {
    if(c.state == State::Dead) return;
    std::string f = frame(msg);
    if(c.state == State::Starting && !force) c.queued.push_back(std::move(f));
    else job_write(c.job, f);
}

Json make_msg(std::string_view method, Json params) {
    Json m;
    m.set("jsonrpc", "2.0");
    m.set("method", method);
    if(!params.is_null()) m.set("params", std::move(params));
    return m;
}

Json error_obj(int code, std::string_view msg) {
    return Json::obj({{"code", code}, {"message", msg}});
}

void reply(Client& c, const Json& id, Json result) {
    Json m;
    m.set("jsonrpc", "2.0");
    m.set("id", id);
    m.set("result", std::move(result));
    send(c, m, true);
}

void reply_error(Client& c, const Json& id, int code, std::string_view msg) {
    Json m;
    m.set("jsonrpc", "2.0");
    m.set("id", id);
    m.set("error", error_obj(code, msg));
    send(c, m, true);
}

void fail(Client& c, std::string reason) {
    if(c.fail_reason.empty()) c.fail_reason = std::move(reason);
    job_kill(c.job);
}

Json section(const Json& root, std::string_view path) {
    const Json* cur = &root;
    while(!path.empty()) {
        const size_t dot = path.find('.');
        cur = &(*cur)[path.substr(0, dot)];
        if(cur->is_null()) return Json();
        path = dot == std::string_view::npos ? std::string_view{}
                                             : path.substr(dot + 1);
    }
    return *cur;
}

void on_server_request(Client& c, const Json& m) {
    const std::string_view method = m["method"].as_str();
    const Json& id = m["id"];
    if(method == "workspace/configuration") {
        Json out = Json::arr();
        for(const Json& item : m["params"]["items"].items())
            out.push(section(c.spec.settings, item["section"].as_str()));
        reply(c, id, std::move(out));
    } else if(method == "client/registerCapability" ||
              method == "client/unregisterCapability" ||
              method == "window/workDoneProgress/create") {
        reply(c, id, Json());
    } else {
        reply_error(c, id, -32601, "method not found");
    }
}

void dispatch(LspId id, const Json& m) {
    Client* c = get(id);
    if(!c) return;
    const bool has_id = m.has("id"), has_method = m.has("method");
    if(has_method && has_id) {
        on_server_request(*c, m);
    } else if(has_method) {
        const auto it = g_notify.find(m["method"].as_str());
        if(it == g_notify.end()) return;
        const LspNotify fn = it->second;
        fn(id, m["params"]);
    } else if(has_id) {
        const auto it = c->pending.find(static_cast<int>(m["id"].as_int(-1)));
        if(it == c->pending.end()) return;
        LspReply cb = std::move(it->second);
        c->pending.erase(it);
        cb(m["result"], m["error"]);
    }
}

void on_data(LspId id, std::string_view d) {
    Client* c = get(id);
    if(!c) return;
    std::vector<std::string> bodies;
    const bool ok = c->reader.feed(d, bodies);
    for(const std::string& b : bodies)
        if(const auto m = Json::parse(b)) dispatch(id, *m);
    if(!ok) fail(*c, "protocol error (bad Content-Length framing)");
}

void on_stderr(LspId id, std::string_view d) {
    Client* c = get(id);
    if(!c) return;
    c->err_tail.append(d);
    constexpr size_t kKeep = 16384;
    if(c->err_tail.size() > kKeep)
        c->err_tail.erase(0, c->err_tail.size() - kKeep);
}

void on_job_exit(LspId id, JobResult r) {
    Client* c = get(id);
    if(!c) return;
    c->state = State::Dead;
    std::string reason = c->fail_reason;
    if(reason.empty()) {
        if(c->stopping) reason = "stopped";
        else if(r.code == 127)
            reason = std::format("command not found: {}",
                                 c->spec.cmd.empty() ? "" : c->spec.cmd[0]);
        else reason = std::format("exited with code {}", r.code);
    }
    auto pending = std::move(c->pending);
    c->pending.clear();
    for(auto& [rid, cb] : pending)
        cb(kNone, error_obj(-32099, "server exited"));
    const auto done = c->spec.on_exit;
    for(auto it = g_clients.begin(); it != g_clients.end(); ++it)
        if((*it)->id == id) {
            g_clients.erase(it);
            break;
        }
    if(done) done(id, reason);
}

void send_initialize(Client& c) {
    const Json caps = Json::obj(
        {{"general",
          Json::obj({{"positionEncodings", Json::arr({"utf-8", "utf-16"})}})},
         {"textDocument",
          Json::obj({{"synchronization", Json::obj({{"didSave", true}})},
                     {"publishDiagnostics", Json::obj()}})},
         {"workspace",
          Json::obj({{"configuration", true}, {"workspaceFolders", true}})}});
    Json p;
    p.set("processId", static_cast<int64_t>(getpid()));
    p.set("clientInfo", Json::obj({{"name", "winter"}}));
    if(c.spec.root.empty()) {
        p.set("rootUri", Json());
    } else {
        const std::string uri = lsp_uri(c.spec.root);
        const size_t slash = c.spec.root.find_last_of('/');
        p.set("rootUri", uri);
        p.set("workspaceFolders",
              Json::arr({Json::obj(
                  {{"uri", uri}, {"name", c.spec.root.substr(slash + 1)}})}));
    }
    p.set("capabilities", caps);
    if(c.spec.init_options.is_object())
        p.set("initializationOptions", c.spec.init_options);
    const int rid = c.next_req++;
    const LspId id = c.id;
    c.pending[rid] = [id](const Json& result, const Json& error) {
        Client* c = get(id);
        if(!c) return;
        if(!error.is_null() || !result.is_object()) {
            fail(*c,
                 std::format("initialize failed: {}",
                             error["message"].as_str("bad reply")));
            return;
        }
        c->caps = result["capabilities"];
        c->encoding = std::string(c->caps["positionEncoding"].as_str("utf-16"));
        job_write(c->job, frame(make_msg("initialized", Json::obj())));
        c->state = State::Ready;
        for(const std::string& f : c->queued) job_write(c->job, f);
        c->queued.clear();
        if(const auto ready = c->spec.on_ready) ready(id);
    };
    Json m = make_msg("initialize", std::move(p));
    m.set("id", rid);
    send(c, m, true);
}

}  // namespace

LspId lsp_start(LspSpec spec) {
    if(spec.cmd.empty()) return 0;
    std::string cmd;
    if(!spec.root.empty()) cmd = "cd " + sh_quote(spec.root) + " && ";
    cmd += "exec";
    for(const std::string& a : spec.cmd) cmd += ' ' + sh_quote(a);

    auto c = std::make_unique<Client>();
    c->id = ++g_next_id;
    const LspId id = c->id;
    c->spec = std::move(spec);

    JobSpec js;
    js.cmd = std::move(cmd);
    js.pipe_stdin = true;
    js.keep_out = false;  // else the job buffers all server output forever
    js.split_err = true;
    js.on_data = [id](std::string_view d) { on_data(id, d); };
    js.on_err = [id](std::string_view d) { on_stderr(id, d); };
    js.on_exit = [id](JobResult r) { on_job_exit(id, std::move(r)); };
    c->job = job_start(std::move(js));
    if(c->job == 0) return 0;
    g_clients.push_back(std::move(c));
    send_initialize(*g_clients.back());
    return id;
}

void lsp_request(LspId id, std::string_view method, Json params, LspReply cb) {
    Client* c = get(id);
    if(!c || c->state == State::Dead) {
        if(cb) cb(kNone, error_obj(-32099, "server not running"));
        return;
    }
    const int rid = c->next_req++;
    if(cb) c->pending[rid] = std::move(cb);
    Json m = make_msg(method, std::move(params));
    m.set("id", rid);
    send(*c, m);
}

void lsp_notify(LspId id, std::string_view method, Json params) {
    Client* c = get(id);
    if(c) send(*c, make_msg(method, std::move(params)));
}

void lsp_stop(LspId id) {
    Client* c = get(id);
    if(!c || c->state == State::Dead || c->stopping) return;
    c->stopping = true;
    if(c->state == State::Ready)
        lsp_request(id, "shutdown", Json(), [id](const Json&, const Json&) {
            lsp_notify(id, "exit");
        });
    else job_kill(c->job);
    loop_after(2000,
               [job = c->job] { job_kill(job); });  // ids are never reused
}

void lsp_on(std::string_view method, LspNotify fn) {
    g_notify[std::string(method)] = std::move(fn);
}

bool lsp_ready(LspId id) {
    const Client* c = get(id);
    return c && c->state == State::Ready;
}
const Json& lsp_capabilities(LspId id) {
    const Client* c = get(id);
    return c ? c->caps : kNone;
}
std::string_view lsp_encoding(LspId id) {
    const Client* c = get(id);
    return c ? std::string_view(c->encoding) : "utf-16";
}
std::string_view lsp_stderr(LspId id) {
    const Client* c = get(id);
    return c ? std::string_view(c->err_tail) : std::string_view{};
}

std::string lsp_uri(std::string_view path) {
    std::string o = "file://";
    for(const unsigned char ch : path) {
        const bool keep = (ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '/' || ch == '-' ||
                          ch == '.' || ch == '_' || ch == '~';
        if(keep) o += static_cast<char>(ch);
        else o += std::format("%{:02X}", ch);
    }
    return o;
}

std::string lsp_path(std::string_view uri) {
    constexpr std::string_view pre = "file://";
    if(!uri.starts_with(pre)) return {};
    uri.remove_prefix(pre.size());
    std::string o;
    for(size_t i = 0; i < uri.size(); ++i) {
        unsigned v = 0;
        if(uri[i] == '%' && i + 2 < uri.size() &&
           std::from_chars(uri.data() + i + 1, uri.data() + i + 3, v, 16).ptr ==
               uri.data() + i + 3) {
            o += static_cast<char>(v);
            i += 2;
        } else {
            o += uri[i];
        }
    }
    return o;
}
