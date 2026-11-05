#include "command.hpp"
#include "external.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "buflist.hpp"
#include "build.hpp"
#include "complete.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "errparse.hpp"
#include "file.hpp"
#include "fuzzy.hpp"
#include "job.hpp"
#include "lsp_actions.hpp"
#include "lsp_config.hpp"
#include "lsp_nav.hpp"
#include "lsp_rename.hpp"
#include "lsp_sync.hpp"
#include "menu.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "parser.hpp"
#include "picker.hpp"
#include "piece.hpp"
#include "prompt.hpp"
#include "quickfix.hpp"
#include "script.hpp"
#include "search.hpp"
#include "syntax.hpp"
#include "terminal.hpp"
#include "theme.hpp"

namespace {
void skip_blanks(std::string_view& s) {
    while(!s.empty() && s.front() == ' ') s.remove_prefix(1);
}

bool is_shell_cmd(std::string_view s) {
    if(s.size() < 2 || s.front() != '$') return false;
    const char n = s[1];
    return n != '+' && n != '-' && n != ',';
}

std::optional<long> parse_addr(std::string_view& s, long base) {
    skip_blanks(s);
    if(s.empty()) return std::nullopt;
    const auto digit = [](char ch) {
        return std::isdigit(static_cast<unsigned char>(ch)) != 0;
    };
    long n = 0;
    const char c = s.front();
    if(digit(c)) {
        while(!s.empty() && digit(s.front())) {
            n = std::min(n * 10 + (s.front() - '0'), 1000000000L);
            s.remove_prefix(1);
        }
    } else if(c == '.') {
        n = base;
        s.remove_prefix(1);
    } else if(c == '$') {
        n = E.num_rows();
        s.remove_prefix(1);
    } else if(c == '+' || c == '-') {
        n = base;
    } else if(s.starts_with("'<") || s.starts_with("'>")) {
        if(!E.vm_valid) return std::nullopt;
        n = static_cast<long>(s[1] == '<' ? E.vm_first : E.vm_last) + 1;
        s.remove_prefix(2);
    } else {
        return std::nullopt;
    }
    while(!s.empty() && (s.front() == '+' || s.front() == '-')) {
        const long sign = s.front() == '+' ? 1 : -1;
        s.remove_prefix(1);
        long d = 0;
        bool any = false;
        while(!s.empty() && digit(s.front())) {
            d = std::min(d * 10 + (s.front() - '0'), 1000000000L);
            s.remove_prefix(1);
            any = true;
        }
        n += sign * (any ? d : 1);
    }
    return n;
}

bool parse_cmdline(std::string_view s, CmdLine& out, std::string& err) {
    const size_t total = s.size();
    skip_blanks(s);
    const long cur = E.cy + 1;
    if(s.starts_with(kVisualRange)) {
        if(!E.vm_valid) {
            err = "No previous visual selection";
            return false;
        }
        out.has_range = true;
        out.first = static_cast<long>(E.vm_first) + 1;
        out.last = static_cast<long>(E.vm_last) + 1;
        s.remove_prefix(kVisualRange.size());
    } else if(s.starts_with('%')) {
        out.has_range = true;
        out.first = 1;
        out.last = E.num_rows();
        s.remove_prefix(1);
    } else if(!is_shell_cmd(s)) {
        const auto a = parse_addr(s, cur);
        skip_blanks(s);
        if(a || s.starts_with(',')) {
            out.has_range = true;
            out.first = out.last = a.value_or(cur);
            if(s.starts_with(',')) {
                s.remove_prefix(1);
                out.last = parse_addr(s, cur).value_or(cur);
            }
        }
    }
    skip_blanks(s);
    if(out.has_range) {
        if(out.first > out.last) std::swap(out.first, out.last);
        if(out.first < 1 || out.last > E.num_rows()) {
            err = "Invalid Range";
            return false;
        }
    }
    out.name_off = total - s.size();
    if(is_shell_cmd(s)) {
        out.name = "$";
        s.remove_prefix(1);
    } else {
        size_t i = 0;
        while(i < s.size() && std::isalpha(static_cast<unsigned char>(s[i])))
            ++i;
        out.name = std::string(s.substr(0, i));
        s.remove_prefix(i);
        if(s.starts_with('!')) {
            out.bang = true;
            s.remove_prefix(1);
        }
    }
    skip_blanks(s);
    out.args_off = total - s.size();
    out.args = std::string(s);
    return true;
}

void goto_line(long ln) {
    auto& pt = E.buf().pt;
    ln = std::clamp(ln, 1L, static_cast<long>(E.num_rows()));
    apply_motion(motion::to_first_nonblank(
        pt, pt.line_start(static_cast<size_t>(ln - 1)), 0));
}

void delete_lines(PieceTable& pt, long f, long l) {
    const size_t lo = pt.line_start(static_cast<size_t>(f - 1));
    const size_t hi = pt.line_end(static_cast<size_t>(l - 1));
    if(hi < pt.size()) pt.remove(lo, hi + 1 - lo);  // take \n after
    else if(lo > 0)
        pt.remove(lo - 1, hi - (lo - 1));  // file tail (take \n before)
    else pt.remove(lo, hi - lo);           // whole buf
}

void move_lines(long f, long l, long dest) {
    auto& pt = E.buf().pt;
    const long n = E.num_rows();
    if(dest >= f && dest < l) {
        editor_set_status_message("Can't move a range of lines into itself");
        return;
    }
    if(dest == f - 1 || dest == l) return;
    const size_t lo = pt.line_start(static_cast<size_t>(f - 1));
    const size_t hi = pt.line_end(static_cast<size_t>(l - 1));
    const std::string blk = pt.text(lo, hi - lo);
    PieceTable::Group g(pt);
    if(dest > l) {
        if(dest < n)
            pt.insert(pt.line_start(static_cast<size_t>(dest)), blk + "\n");
        else pt.insert(pt.size(), "\n" + blk);
        delete_lines(pt, f, l);
    } else {
        delete_lines(pt, f, l);
        pt.insert(pt.line_start(static_cast<size_t>(dest)), blk + "\n");
    }
    const long cnt = l - f + 1;
    goto_line(dest > l ? dest : dest + cnt);
    if(cnt > 2) editor_set_status_message("{} lines moved", cnt);
}

void cmd_move(const CmdLine& c) {
    std::string_view a = c.args;
    const long l = c.has_range ? c.last : E.cy + 1;
    const long f = c.has_range ? c.first : l;
    skip_blanks(a);
    const long base = a.starts_with('-') ? f : l;
    const auto dest = parse_addr(a, base);
    skip_blanks(a);
    if(!dest || !a.empty() || *dest < 0 || *dest > E.num_rows()) {
        editor_set_status_message("Invalid address: {}", c.args);
        return;
    }
    move_lines(f, l, *dest);
}

std::vector<std::string> split_delim(std::string_view s, char d) {
    std::vector<std::string> parts(1);
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == '\\' && i + 1 < s.size()) {
            if(s[i + 1] == d) parts.back() += d;
            else {
                parts.back() += s[i];
                parts.back() += s[i + 1];
            }
            ++i;
        } else if(s[i] == d) {
            parts.emplace_back();
        } else {
            parts.back() += s[i];
        }
    }
    return parts;
}

// Splits on blanks. '...' is literal, "..." honours \" and \\, and a bare
// backslash escapes the next char. Returns false on an unterminated quote.
bool split_argv(std::string_view s, std::vector<std::string>& out,
                std::string& err) {
    out.clear();
    std::string cur;
    bool in_arg = false;
    for(size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if(c == ' ' || c == '\t') {
            if(in_arg) {
                out.push_back(std::move(cur));
                cur.clear();
                in_arg = false;
            }
        } else if(c == '\'') {
            in_arg = true;
            const size_t e = s.find('\'', i + 1);
            if(e == std::string_view::npos) {
                err = "Unterminated '";
                return false;
            }
            cur.append(s.substr(i + 1, e - i - 1));
            i = e;
        } else if(c == '"') {
            in_arg = true;
            size_t j = i + 1;
            for(;; ++j) {
                if(j >= s.size()) {
                    err = "Unterminated \"";
                    return false;
                }
                if(s[j] == '"') break;
                if(s[j] == '\\' && j + 1 < s.size() &&
                   (s[j + 1] == '"' || s[j + 1] == '\\'))
                    ++j;
                cur += s[j];
            }
            i = j;
        } else if(c == '\\' && i + 1 < s.size()) {
            in_arg = true;
            cur += s[++i];
        } else {
            in_arg = true;
            cur += c;
        }
    }
    if(in_arg) out.push_back(std::move(cur));
    return true;
}

bool validate_cmd(CmdLine& c, std::string& err) {
    const CmdSpec& sp = *c.spec;
    if(c.bang && !sp.bang) {
        err = std::format("No ! allowed for :{}", sp.name);
        return false;
    }
    if(c.has_range && !sp.range) {
        err = std::format("No range allowed for :{}", sp.name);
        return false;
    }
    const auto bad_args = [&] {
        err = sp.usage.empty()
                  ? std::format("Wrong number of arguments for :{}", sp.name)
                  : std::format("Usage: {}", sp.usage);
        return false;
    };
    if(sp.raw) {  // command parses c.args itself; only check empty/non-empty
        if(sp.nargs == Nargs::None && !c.args.empty()) return bad_args();
        if(sp.nargs == Nargs::One && c.args.empty()) return bad_args();
        return true;
    }
    if(!split_argv(c.args, c.argv, err)) return false;
    const size_t n = c.argv.size();
    switch(sp.nargs) {
        case Nargs::None:
            if(n != 0) return bad_args();
            break;
        case Nargs::One:
            if(n != 1) return bad_args();
            break;
        case Nargs::Opt:
            if(n > 1) return bad_args();
            break;
        case Nargs::Any: break;
    }
    return true;
}

std::string to_format(std::string_view r) {
    std::string out;
    for(size_t i = 0; i < r.size(); ++i) {
        const char c = r[i];
        if(c == '\\' && i + 1 < r.size()) {
            const char n = r[++i];
            if(n == '0') out += "$&";
            else if(n >= '1' && n <= '9') {
                out += '$';
                out += n;
            } else if(n == 'r') out += '\n';
            else if(n == 't') out += '\t';
            else if(n == '$') out += "$$";
            else out += n;
        } else if(c == '&') out += "$&";
        else if(c == '$') out += "$$";
        else out += c;
    }
    return out;
}

struct SubstSpec {
    std::string pat;
    bool has_rep = false;
    bool global = false;
    std::regex re;
    std::string fmt;
};

std::optional<SubstSpec> parse_subst(std::string_view args, std::string& err) {
    if(args.size() < 2) {
        err = "Usage: :s/pattern/replacement/[gi]";
        return std::nullopt;
    }
    const char d = args[0];
    if(std::isalnum(static_cast<unsigned char>(d)) || d == '\\' || d == ' ' ||
       d == '"' || d == '|') {
        err = std::format("Bad delim: {}", d);
        return std::nullopt;
    }
    const auto parts = split_delim(args.substr(1), d);
    SubstSpec sp;
    sp.pat = parts[0];
    sp.has_rep = parts.size() > 1;
    const std::string rep = sp.has_rep ? parts[1] : "";
    const std::string flags = parts.size() > 2 ? parts[2] : "";
    if(sp.pat.empty()) {
        err = "Empty pattern";
        return std::nullopt;
    }
    bool icase = false;
    for(const char f : flags) {
        if(f == 'g') sp.global = true;
        else if(f == 'i') icase = true;
        else if(f == 'I') icase = false;
        else {
            err = std::format("Unknown Flag: {}", f);
            return std::nullopt;
        }
    }
    auto syntax = std::regex::ECMAScript;
    if(icase) syntax |= std::regex::icase;
    try {
        sp.re = std::regex(sp.pat, syntax);
    } catch(const std::regex_error&) {
        err = std::format("Invalid pattern: {}", sp.pat);
        return std::nullopt;
    }
    sp.fmt = to_format(rep);
    return sp;
}

void cmd_substitute(const CmdLine& c) {
    std::string err;
    const auto sp = parse_subst(c.args, err);
    if(!sp) {
        editor_set_status_message("{}", err);
        return;
    }
    const std::string& pat = sp->pat;
    const std::regex& re = sp->re;
    const std::string& fmt = sp->fmt;
    const bool global = sp->global;
    const auto mflags = global ? std::regex_constants::format_default
                               : std::regex_constants::format_first_only;
    const long first = c.has_range ? c.first : E.cy + 1;
    const long last = c.has_range ? c.last : first;
    auto& pt = E.buf().pt;
    PieceTable::Group g(pt);
    size_t subs = 0, lines = 0;
    long last_hit = -1;
    for(long ln = last; ln >= first; --ln) {
        const size_t l = static_cast<size_t>(ln - 1);
        const std::string line = pt.line_text(l);
        if(!std::regex_search(line, re)) continue;
        subs += global ? static_cast<size_t>(std::distance(
                             std::sregex_iterator(line.begin(), line.end(), re),
                             std::sregex_iterator()))
                       : 1;
        ++lines;
        if(last_hit < 0) last_hit = ln;
        const std::string out = std::regex_replace(line, re, fmt, mflags);
        if(out == line) continue;
        const size_t s = pt.line_start(l);
        if(!line.empty()) pt.remove(s, line.size());
        pt.insert(s, out);
    }
    if(!lines) {
        editor_set_status_message("Pattern not found: {}", pat);
        return;
    }
    goto_line(last_hit);
    editor_set_status_message("{} substitutions on {} lines", subs, lines);
}

static bool quit_blocked() {
    const auto m = buffer_first_modified();
    if(!m) return false;
    editor_set_status_message(
        "Unsaved changes in buffer {} ({}); q! to quit anyways",
        *m + 1,
        buffer_name(*m));
    return true;
}

std::string first_line(const std::string& s) {
    return s.substr(0, s.find('\n'));
}

void show_output(std::string out) {
    while(!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    if(out.empty()) editor_set_status_message("(No Output)");
    else if(out.find('\n') == std::string::npos)
        editor_set_status_message("{}", out);
    else buffer_show_text("[shell]", out);
}

void apply_filter(const Buffer* target, int first, int last,
                  const std::string& cmd, const std::string& orig,
                  JobResult r) {
    if(r.code != 0) {
        editor_set_status_message(
            "Filter failed (exit {}): {}", r.code, first_line(r.out));
        return;
    }
    if(&E.buf() != target || E.num_rows() < last) {
        editor_set_status_message("Filter dropped: buffer changed while {} ran",
                                  cmd);
        return;
    }
    auto& pt = E.buf().pt;
    const size_t lo = pt.line_start(static_cast<size_t>(first - 1));
    const size_t hi = pt.line_start(static_cast<size_t>(last - 1));
    if(pt.text(lo, hi - lo) != orig) {
        editor_set_status_message("Filter dropped: lines changed while {} ran",
                                  cmd);
        return;
    }
    std::string out = std::move(r.out);
    const bool empty = out.empty();
    if(out.ends_with('\n')) out.pop_back();
    {
        PieceTable::Group g(pt);
        if(empty) delete_lines(pt, first, last);
        else {
            if(hi > lo) pt.remove(lo, hi - lo);
            pt.insert(lo, out);
        }
    }
    goto_line(first);
    editor_set_status_message(
        "{} lines filtered through {}", last - first + 1, cmd);
}

void cmd_shell(const CmdLine& c) {
    if(c.args.empty()) {
        editor_set_status_message("Usage: $ <Command>");
        return;
    }
    if(!c.has_range) {
        const std::string cmd = c.args;
        const JobId id = job_start({.cmd = cmd, .on_exit = [](JobResult r) {
                                        show_output(std::move(r.out));
                                    }});
        if(!id) editor_set_status_message("Can't run: {}", cmd);
        else editor_set_status_message("$ {} ...", cmd);
        return;
    }
    const Buffer* const target = &E.buf();
    auto& pt = E.buf().pt;
    const size_t lo = pt.line_start(static_cast<size_t>(c.first - 1));
    const size_t hi = pt.line_start(static_cast<size_t>(c.last - 1));
    std::string orig = pt.text(lo, hi - lo);
    std::string in = orig;
    in += '\n';
    const int first = c.first, last = c.last;
    const std::string cmd = c.args;
    const JobId id = job_start(
        {.cmd = cmd,
         .input = std::move(in),
         .on_exit = [=, orig = std::move(orig)](JobResult r) {
             apply_filter(target, first, last, cmd, orig, std::move(r));
         }});
    if(!id) editor_set_status_message("Can't run: {}", cmd);
    else editor_set_status_message("Filtering through {} ...", cmd);
}

void reattach_syntax(std::string_view lang) {
    for(auto& b : E.buffers) {
        if(b->nofile) continue;
        if(Syntax::detect(b->filename) != lang) continue;
        if(b->syntax.attach(b->filename)) b->syntax.parse(b->pt, b->eol);
    }
}

void cmd_copy(const CmdLine& c) {
    auto& pt = E.buf().pt;
    std::string_view a = c.args;
    const long l = c.has_range ? c.last : E.cy + 1;
    const long f = c.has_range ? c.first : l;
    skip_blanks(a);
    const auto dest = parse_addr(a, E.cy + 1);
    skip_blanks(a);
    if(!dest || !a.empty() || *dest < 0 || *dest > E.num_rows()) {
        editor_set_status_message("Invalid address: {}", c.args);
        return;
    }
    const size_t lo = pt.line_start(static_cast<size_t>(f - 1));
    const size_t hi = pt.line_end(static_cast<size_t>(l - 1));
    const std::string blk = pt.text(lo, hi - lo);
    PieceTable::Group g(pt);
    if(*dest < E.num_rows())
        pt.insert(pt.line_start(static_cast<size_t>(*dest)), blk + "\n");
    else pt.insert(pt.size(), "\n" + blk);
    const long cnt = l - f + 1;
    goto_line(*dest + cnt);
    if(cnt > 2) editor_set_status_message("{} more lines", cnt);
}

void cmd_tsinstall(const CmdLine& c) {
    if(c.args.empty()) {
        editor_set_status_message("Grammars (* installed): {}", parser_list());
        return;
    }
    const std::string lang = c.args;
    if(!parser_known(lang)) {
        editor_set_status_message("Unknown grammar '{}'. :TSInstall lists them",
                                  lang);
        return;
    }
    if(parser_installed(lang) && !c.bang) {
        editor_set_status_message(
            "{} is installed; :TSInstall! {} rebuilds it", lang, lang);
        return;
    }
    const JobId id = parser_install(
        lang,
        [lang](std::string_view line) {
            editor_set_status_message("TSInstall {}: {}", lang, line);
        },
        [lang](bool ok, std::string msg) {
            if(!ok) {
                editor_set_status_message("TSInstall {} failed: {}", lang, msg);
                return;
            }
            reattach_syntax(lang);
            editor_set_status_message("TSInstall {}: installed", lang);
        });
    if(!id) editor_set_status_message("TSInstall {}: already running", lang);
}

void cmd_lsp(const CmdLine& c) {
    const std::string_view args = c.args;
    const size_t sp = args.find(' ');
    const std::string_view sub = args.substr(0, sp);
    std::string_view name =
        sp == std::string_view::npos ? std::string_view{} : args.substr(sp + 1);
    skip_blanks(name);
    if(sub == "status") {
        buffer_show_text("[lsp]", lsp_sync_server_report());
        return;
    }
    if(sub != "start" && sub != "stop" && sub != "restart") {
        std::string known;
        for(const std::string& n : lsp_enabled_names())
            known += (known.empty() ? "" : ", ") + n;
        editor_set_status_message(
            "usage: :lsp start|stop|restart [server] (registered: {})",
            known.empty() ? "none" : known);
        return;
    }
    const std::string cur =
        name.empty() ? lsp_sync_server_of(E.buf()) : std::string{};
    if(name.empty()) name = cur;
    if(name.empty()) {
        editor_set_status_message("lsp {}: no server for this buffer", sub);
        return;
    }
    const std::string msg = sub == "start"  ? lsp_sync_server_start(name)
                            : sub == "stop" ? lsp_sync_server_stop(name)
                                            : lsp_sync_server_restart(name);
    if(!msg.empty()) editor_set_status_message("{}", msg);
}

std::vector<std::unique_ptr<CmdSpec>>& specs() {
    static std::vector<std::unique_ptr<CmdSpec>> v;
    return v;
}

// canonical names and aliases, all pointing at the owning spec
std::map<std::string, CmdSpec*, std::less<>>& index() {
    static std::map<std::string, CmdSpec*, std::less<>> m;
    return m;
}

}  // namespace

void command_add(CmdSpec spec) {
    auto& idx = index();
    CmdSpec* slot = nullptr;
    for(auto& p : specs())
        if(p->name == spec.name) {
            slot = p.get();
            break;
        }
    if(slot) {  // re-registration: drop old names, overwrite in place
        std::erase_if(idx,
                      [slot](const auto& kv) { return kv.second == slot; });
        *slot = std::move(spec);
    } else {
        specs().push_back(std::make_unique<CmdSpec>(std::move(spec)));
        slot = specs().back().get();
    }
    idx[slot->name] = slot;
    for(const std::string& a : slot->aliases) idx[a] = slot;
}

void command_add(std::string name, CmdFn fn) {
    command_add(CmdSpec{.name = std::move(name),
                        .range = true,
                        .bang = true,
                        .nargs = Nargs::Any,
                        .raw = true,
                        .run = std::move(fn)});
}

bool command_remove(std::string_view name) {
    auto& v = specs();
    const auto it = std::find_if(
        v.begin(), v.end(), [&](const auto& p) { return p->name == name; });
    if(it == v.end()) return false;
    const CmdSpec* slot = it->get();
    std::erase_if(index(),
                  [slot](const auto& kv) { return kv.second == slot; });
    v.erase(it);
    return true;
}

CmdMatch command_find(std::string_view name) {
    CmdMatch m;
    if(name.empty()) return m;
    const auto& idx = index();
    if(const auto it = idx.find(name); it != idx.end()) {  // exact / alias
        m.spec = it->second;
        return m;
    }
    // tier 1: prefix of a canonical name; tier 2: prefix of an alias
    for(const bool canonical : {true, false}) {
        std::vector<const CmdSpec*> hits;
        for(auto it = idx.lower_bound(name);
            it != idx.end() && it->first.starts_with(name);
            ++it) {
            const bool is_canon = it->first == it->second->name;
            if(is_canon != canonical) continue;
            if(std::ranges::find(hits, it->second) == hits.end())
                hits.push_back(it->second);
        }
        if(hits.size() == 1) {
            m.spec = hits[0];
            return m;
        }
        if(hits.size() > 1) {
            for(const CmdSpec* s : hits) m.cands.push_back(s->name);
            return m;
        }
    }
    return m;
}

// ---- completers (CmdSpec::complete) ----------------------------------------
static std::vector<CmdCand> complete_files(std::string_view lead) {
    namespace fs = std::filesystem;
    std::vector<CmdCand> out;
    const size_t slash = lead.rfind('/');
    const std::string dir = slash == std::string_view::npos
                                ? ""
                                : std::string(lead.substr(0, slash + 1));
    const std::string stem(
        slash == std::string_view::npos ? lead : lead.substr(slash + 1));
    std::error_code ec;
    fs::directory_iterator it(dir.empty() ? "." : dir, ec);
    if(ec) return out;
    for(const fs::directory_iterator end; it != end && out.size() < 500;
        it.increment(ec)) {
        if(ec) break;
        const std::string name = it->path().filename().string();
        if(name.starts_with('.') && !stem.starts_with('.')) continue;
        const bool is_dir = it->is_directory(ec);
        out.push_back({dir + name + (is_dir ? "/" : ""), is_dir ? "dir" : ""});
    }
    return out;
}

static std::vector<CmdCand> complete_buffers(std::string_view) {
    std::vector<CmdCand> out;
    for(size_t i = 0; i < E.buffers.size(); ++i)
        out.push_back({buffer_name(i), std::to_string(i + 1)});
    return out;
}

static std::vector<CmdCand> complete_lsp(std::string_view) {
    std::vector<CmdCand> out = {{"start", "subcommand"},
                                {"stop", "subcommand"},
                                {"restart", "subcommand"},
                                {"status", "subcommand"}};
    for(const std::string& n : lsp_enabled_names())
        out.push_back({n, "server"});
    return out;
}

void register_builtin_commands() {
    command_add({.name = "nohlsearch",
                 .aliases = {"noh"},
                 .desc = "Clear search highlight",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { search_clear_highlight(); }});
    command_add({.name = "quit",
                 .aliases = {"q"},
                 .desc = "Quit the editor",
                 .bang = true,
                 .nargs = Nargs::None,
                 .run = [](const CmdLine& c) {
                     if(!c.bang && quit_blocked()) return;
                     editor_quit();
                 }});
    command_add({.name = "write",
                 .aliases = {"w"},
                 .desc = "Save the buffer",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { editor_save(); }});
    command_add({.name = "wall",
                 .aliases = {"wa", "writeall"},
                 .desc = "Save all modified buffers",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { editor_save_all(); }});
    command_add({.name = "wq",
                 .aliases = {"x"},
                 .desc = "Save and quit",
                 .bang = true,
                 .nargs = Nargs::None,
                 .run = [](const CmdLine& c) {
                     editor_save();
                     if(c.bang || !quit_blocked()) editor_quit();
                 }});
    command_add({.name = "edit",
                 .aliases = {"e"},
                 .desc = "Open a file",
                 .usage = ":e <file> | :e[!]  (reload from disk)",
                 .bang = true,
                 .nargs = Nargs::Opt,
                 .complete = complete_files,
                 .run = [](const CmdLine& c) {
                     if(c.argv.empty()) external_reload(E.buf(), c.bang);
                     else buffer_open(c.argv[0]);
                 }});
    command_add({.name = "ls",
                 .aliases = {"buffers"},
                 .desc = "List buffers",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) {
                     editor_set_status_message("{}", buffer_list());
                 }});
    command_add({.name = "bnext",
                 .aliases = {"bn"},
                 .desc = "Next buffer",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { buffer_cycle(+1); }});
    command_add({.name = "bprev",
                 .aliases = {"bp"},
                 .desc = "Previous buffer",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { buffer_cycle(-1); }});
    command_add({.name = "bdelete",
                 .aliases = {"bd"},
                 .desc = "Close buffer",
                 .bang = true,
                 .nargs = Nargs::None,
                 .run = [](const CmdLine& c) { buffer_close(E.cur, c.bang); }});
    command_add({.name = "buffer",
                 .aliases = {"b"},
                 .desc = "Go to buffer",
                 .usage = ":b <number|name>",
                 .nargs = Nargs::Opt,
                 .raw = true,
                 .complete = complete_buffers,
                 .run = [](const CmdLine& c) { buffer_goto(c.args); }});
    command_add({.name = "set",
                 .aliases = {"se"},
                 .desc = "Set options",
                 .nargs = Nargs::Any,
                 .raw = true,
                 .run = [](const CmdLine& c) {
                     const std::string m = apply_set_command(c.args);
                     if(!m.empty()) editor_set_status_message("{}", m);
                 }});
    command_add({.name = "lua",
                 .desc = "Evaluate Lua",
                 .nargs = Nargs::One,
                 .raw = true,
                 .run = [](const CmdLine& c) { script_eval(c.args); }});
    command_add({.name = "source",
                 .desc = "Run a Lua file (no reset); default: this buffer",
                 .usage = ":source [file]",
                 .nargs = Nargs::Opt,
                 .raw = true,
                 .run = [](const CmdLine& c) {
                     const std::string path =
                         c.args.empty() ? E.buf().filename : c.args;
                     if(path.empty()) {
                         editor_set_status_message("source: no file");
                         return;
                     }
                     if(script_source(path))
                         editor_set_status_message("Sourced {}", path);
                 }});
    command_add({.name = "reload",
                 .desc = "Reset the config state and re-run init.lua",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) { script_reload_later(); }});
    command_add({.name = "semstat",
                 .desc = "LSP semantic token report",
                 .nargs = Nargs::Opt,
                 .raw = true,
                 .run = [](const CmdLine& c) {
                     Buffer& b = E.buf();
                     if(c.args == "debug") {
                         const bool on = lsp_sync_semantic_debug(b);
                         editor_set_status_message(
                             "semstat: debug underline {}, tokens repaint on "
                             "the next reply",
                             on ? "on" : "off");
                         return;
                     }
                     buffer_show_text("[semstat]", lsp_sync_semantic_report(b));
                 }});
    command_add({.name = "move",
                 .aliases = {"m"},
                 .desc = "Move lines",
                 .usage = ":[range]m <address>",
                 .range = true,
                 .nargs = Nargs::One,
                 .raw = true,
                 .run = cmd_move});
    command_add({.name = "copy",
                 .aliases = {"t", "co"},
                 .desc = "Copy lines",
                 .usage = ":[range]t <address>",
                 .range = true,
                 .nargs = Nargs::One,
                 .raw = true,
                 .run = cmd_copy});
    command_add({.name = "substitute",
                 .aliases = {"s"},
                 .desc = "Substitute with regex",
                 .usage = ":[range]s/pattern/replacement/[gi]",
                 .range = true,
                 .nargs = Nargs::One,
                 .raw = true,
                 .run = cmd_substitute});
    command_add({.name = "$",
                 .desc = "Run a shell command (range: filter)",
                 .usage = ":[range]$ <command>",
                 .range = true,
                 .nargs = Nargs::One,
                 .raw = true,
                 .run = cmd_shell});
    command_add({.name = "TSInstall",
                 .desc = "Install a tree-sitter grammar",
                 .usage = ":TSInstall[!] [lang]",
                 .bang = true,
                 .nargs = Nargs::Opt,
                 .raw = true,
                 .run = cmd_tsinstall});
    command_add({.name = "lsp",
                 .desc = "Start, stop or restart LSP server",
                 .usage = ":lsp start|stop|restart [server]",
                 .nargs = Nargs::Any,
                 .raw = true,
                 .complete = complete_lsp,
                 .run = cmd_lsp});
    errparser_register_builtin();
    qf_register_commands();
    build_register_builtin();
    build_register_commands();
    picker_register_commands();
    lsp_nav_register_commands();
    lsp_actions_register();
    lsp_rename_register();
    completion_register_commands();
}

void editor_command_palette(std::string_view cmd) {
    while(cmd.ends_with(' ')) cmd.remove_suffix(1);
    CmdLine c;
    std::string err;
    if(!parse_cmdline(cmd, c, err)) {
        editor_set_status_message("{}", err);
        return;
    }
    if(c.name.empty()) {
        if(c.has_range) goto_line(c.last);
        return;
    }
    const CmdMatch m = command_find(c.name);
    if(!m.spec) {
        if(m.cands.empty()) {
            editor_set_status_message("Unknown Command: {}", c.name);
            return;
        }
        std::string list;
        for(const std::string& n : m.cands) {
            if(!list.empty()) list += ", ";
            list += n;
        }
        editor_set_status_message("Ambiguous command {}: {}", c.name, list);
        return;
    }
    c.spec = m.spec;
    if(!validate_cmd(c, err)) {
        editor_set_status_message("{}", err);
        return;
    }
    const CmdFn fn = m.spec->run;  // copy: the command may re-register itself
    if(fn) fn(c);
}

// ---- the ':' completion popup ---------------------------------------------
namespace {

struct Popup {
    Menu menu;
    std::vector<std::string> inserts;  // MenuItem::id indexes this
    size_t from = 0, to = 0;           // span of the line a pick replaces
    bool name_ctx = false;
    bool forced = false;  // Tab opened it on an empty word
} g_pop;

struct PopCtx {
    bool name = false;  // completing the command name (else an argument)
    size_t from = 0, to = 0;
    std::string lead;  // the word before the cursor
    const CmdSpec* spec = nullptr;
};

void pop_reset() {
    g_pop.menu.close();
    g_pop.forced = false;
}

bool pop_context(const std::string& buf, size_t pos, PopCtx& ctx) {
    const std::string head = buf.substr(0, pos);
    CmdLine c;
    std::string err;
    if(!parse_cmdline(head, c, err) || c.name == "$") return false;

    const bool in_name = c.args.empty() && !c.bang && !head.ends_with(' ');
    if(in_name) {
        if(c.name.empty() && c.has_range) return false;  // ":12"
        ctx.name = true;
        ctx.from = c.name_off;
        ctx.to = pos;
        while(ctx.to < buf.size() &&
              std::isalpha(static_cast<unsigned char>(buf[ctx.to])))
            ++ctx.to;
        ctx.lead = c.name;
        return true;
    }

    if(c.name.empty()) return false;
    if(c.args.empty() && !head.ends_with(' ')) return false;  // ":e!"
    const CmdMatch m = command_find(c.name);
    if(!m.spec || !m.spec->complete) return false;
    const size_t ws = head.find_last_of(' ');
    ctx.from =
        std::max(c.args_off, ws == std::string::npos ? size_t{0} : ws + 1);
    ctx.to = pos;
    while(ctx.to < buf.size() && buf[ctx.to] != ' ') ++ctx.to;
    ctx.lead = head.substr(ctx.from);
    ctx.spec = m.spec;
    return true;
}

void pop_update(const std::string& buf, size_t pos) {
    PopCtx ctx;
    if(!pop_context(buf, pos, ctx) ||
       (ctx.name && ctx.lead.empty() && !g_pop.forced))
        return g_pop.menu.close();

    struct Hit {
        int score;
        std::string insert;
        MenuItem item;
    };
    std::vector<Hit> hits;
    fuzzy::Match m;
    if(ctx.name) {
        for(const auto& sp : specs()) {
            int best = 0;
            std::vector<size_t> hl;
            if(!ctx.lead.empty()) {
                bool ok = false;
                if(fuzzy::match(sp->name, ctx.lead, m)) {
                    ok = true;
                    best = m.score;
                    hl = m.pos;
                }
                for(const std::string& a : sp->aliases)
                    if(fuzzy::match(a, ctx.lead, m) &&
                       (!ok || m.score > best)) {
                        ok = true;
                        best = m.score;
                        hl.clear();  // the match was on the alias, not the
                                     // label
                    }
                if(!ok) continue;
            }
            hits.push_back({best,
                            sp->name,
                            MenuItem{.label = sp->name,
                                     .detail = sp->desc,
                                     .hl = std::move(hl)}});
        }
    } else {
        for(CmdCand& cand : ctx.spec->complete(ctx.lead)) {
            if(!ctx.lead.empty() && !fuzzy::match(cand.text, ctx.lead, m))
                continue;
            if(ctx.lead.empty()) m = {};
            hits.push_back({m.score,
                            cand.text,
                            MenuItem{.label = cand.text,
                                     .detail = std::move(cand.detail),
                                     .hl = m.pos}});
        }
    }
    if(hits.empty()) return g_pop.menu.close();

    std::ranges::stable_sort(hits, [](const Hit& a, const Hit& b) {
        return a.score != b.score ? a.score > b.score
                                  : a.item.label < b.item.label;
    });
    if(hits.size() > 200) hits.erase(hits.begin() + 200, hits.end());

    std::vector<MenuItem> items;
    g_pop.inserts.clear();
    for(Hit& h : hits) {
        h.item.id = g_pop.inserts.size();
        g_pop.inserts.push_back(std::move(h.insert));
        items.push_back(std::move(h.item));
    }
    g_pop.from = ctx.from;
    g_pop.to = ctx.to;
    g_pop.name_ctx = ctx.name;
    if(!g_pop.menu.is_open())
        g_pop.menu.open({.at = OverlaySpec::At::Screen,
                         .x = 0,
                         .y = E.term_rows + 1,
                         .up = true,
                         .cover_bars = true,
                         .in_prompt = true,
                         .max_rows = 8,
                         .min_w = 24,
                         .max_w = 70});
    g_pop.menu.set_items(std::move(items));
}

bool pop_accept(std::string& buf, size_t& pos) {
    const MenuItem* it = g_pop.menu.selected();
    if(!g_pop.menu.visible() || !it) return false;
    const std::string text = g_pop.inserts[it->id];
    const size_t from = std::min(g_pop.from, buf.size());
    const size_t to = std::clamp(g_pop.to, from, buf.size());
    buf.replace(from, to - from, text);
    pos = from + text.size();
    if(g_pop.name_ctx) {  // a command name is followed by its arguments
        if(pos < buf.size() && buf[pos] == ' ') ++pos;
        else buf.insert(pos++, 1, ' ');
    }
    return true;
}

bool popup_pre_key(int key, std::string& buf, size_t& pos) {
    switch(key) {
        case '\t':
            if(g_pop.menu.visible()) {
                if(pop_accept(buf, pos)) pop_update(buf, pos);
            } else {
                g_pop.forced = true;  // Tab on an empty word lists everything
                pop_update(buf, pos);
            }
            return true;
        case CTRL_KEY('y'):
            if(!g_pop.menu.visible()) return false;
            if(pop_accept(buf, pos)) pop_update(buf, pos);
            return true;
        case CTRL_KEY('n'):
        case CTRL_KEY('p'):
            if(!g_pop.menu.visible()) return false;  // fall through to history
            g_pop.menu.move(key == CTRL_KEY('n') ? +1 : -1);
            return true;
        default: return false;
    }
}

void popup_after_key(const std::string& buf, size_t pos, int key, bool edited) {
    if(key == '\x1b' || key == '\r') return pop_reset();
    if(edited) pop_update(buf, pos);
    else g_pop.menu.close();  // cursor moved or history browsed
}

}  // namespace

void editor_command_prompt() {
    PromptHooks hooks;
    hooks.keymap_mode = COMMAND;
    hooks.pre_key = popup_pre_key;
    hooks.after_key =
        [](const std::string& buf, size_t pos, int key, bool edited) {
            subst_preview_key(buf, key);
            popup_after_key(buf, pos, key, edited);
        };
    std::string cmd =
        editor_prompt(":", hooks, std::exchange(E.cmd_prefill, std::string{}));
    pop_reset();
    E.cmd_sel = false;
    set_mode(NORMAL);
    if(cmd.empty()) return;
    editor_command_palette(cmd);
}

namespace {
struct {
    bool on = false;
    long first = 0, last = 0;
    bool show_rep = false, global = false;
    std::regex re;
    std::string fmt;
} g_sp;
}  // namespace

size_t SubstView::map(size_t off) const {
    long d = 0;
    for(const Edit& e : edits) {
        if(off <= e.lo) break;
        if(off < e.hi) return e.nlo + std::min(off - e.lo, e.nhi - e.nlo);
        d = static_cast<long>(e.nhi) - static_cast<long>(e.hi);
    }
    return static_cast<size_t>(static_cast<long>(off) + d);
}

void subst_preview_key(std::string cmd, int key) {
    g_sp.on = false;
    if(key == '\x1b' || key == '\r') return;
    while(cmd.ends_with(' ')) cmd.pop_back();
    CmdLine c;
    std::string err;
    if(!parse_cmdline(cmd, c, err)) return;
    const CmdMatch m = command_find(c.name);
    if(!m.spec || m.spec->name != "substitute") return;
    auto sp = parse_subst(c.args, err);
    if(!sp) return;
    g_sp.first = c.has_range ? c.first : E.cy + 1;
    g_sp.last = c.has_range ? c.last : g_sp.first;
    g_sp.show_rep = sp->has_rep;
    g_sp.global = sp->global;
    g_sp.re = std::move(sp->re);
    g_sp.fmt = std::move(sp->fmt);
    g_sp.on = true;
}

bool subst_preview_line(int row, std::string_view line, SubstView& out) {
    if(!g_sp.on || row + 1 < g_sp.first || row + 1 > g_sp.last) return false;
    if(line.size() > 4096) return false;
    const Style match_st =
        theme_ui("ui.subst.match", Style{.fg = 0x1e1e2e, .bg = 0xf38ba8});
    const Style rep_st =
        theme_ui("ui.subst.replace",
                 Style{.fg = 0x1e1e2e, .bg = 0xa6e3a1, .attrs = ATTR_BOLD});
    out.text.clear();
    out.marks.clear();
    out.edits.clear();
    const std::string s(line);
    size_t copied = 0;
    for(std::sregex_iterator it(s.begin(), s.end(), g_sp.re), end; it != end;
        ++it) {
        const std::smatch& m = *it;
        const size_t lo = static_cast<size_t>(m.position(0));
        const size_t hi = lo + static_cast<size_t>(m.length(0));
        out.text.append(s, copied, lo - copied);
        const size_t nlo = out.text.size();
        if(g_sp.show_rep) {
            std::string r = m.format(g_sp.fmt);
            for(size_t k; (k = r.find('\n')) != std::string::npos;)
                r.replace(k, 1, "\u21b5");
            out.text += r;
        } else {
            out.text.append(s, lo, hi - lo);
        }
        out.edits.push_back({lo, hi, nlo, out.text.size()});
        out.marks.push_back({nlo,
                             out.text.size(),
                             kPrioSubst,
                             g_sp.show_rep ? rep_st : match_st});
        copied = hi;
        if(!g_sp.global) break;
    }
    if(out.edits.empty()) return false;
    out.text.append(s, copied, std::string::npos);
    return true;
}
