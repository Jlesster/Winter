#include "command.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "buflist.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "job.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "parser.hpp"
#include "piece.hpp"
#include "prompt.hpp"
#include "script.hpp"
#include "syntax.hpp"
#include "terminal.hpp"

namespace {
struct CmdLine {
    bool has_range = false;
    long first = 0, last = 0;
    std::string name;
    bool bang = false;
    std::string args;
};

void skip_blanks(std::string_view& s) {
    while(!s.empty() && s.front() == ' ') s.remove_prefix(1);
}

std::optional<long> parse_addr(std::string_view& s) {
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
        n = E.cy + 1;
        s.remove_prefix(1);
    } else if(c == '$') {
        n = E.num_rows();
        s.remove_prefix(1);
    } else if(c == '+' || c == '-') {
        n = E.cy + 1;
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
    } else if(!s.starts_with("$ ")) {
        const auto a = parse_addr(s);
        skip_blanks(s);
        if(a || s.starts_with(',')) {
            out.has_range = true;
            out.first = out.last = a.value_or(cur);
            if(s.starts_with(',')) {
                s.remove_prefix(1);
                out.last = parse_addr(s).value_or(cur);
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
    if(s.starts_with("$ ")) {
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
    const auto dest = parse_addr(a);
    skip_blanks(a);
    if(!dest || !a.empty() || *dest < 0 || *dest > E.num_rows()) {
        editor_set_status_message("Invalid address: {}", c.args);
        return;
    }
    const long f = c.has_range ? c.first : E.cy + 1;
    const long l = c.has_range ? c.last : f;
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

void cmd_substitute(const CmdLine& c) {
    if(c.args.size() < 2) {
        editor_set_status_message("Usage: :s/pattern/replacement/[gi]");
        return;
    }
    const char d = c.args[0];
    if(std::isalnum(static_cast<unsigned char>(d)) || d == '\\' || d == ' ' ||
       d == '"' || d == '|') {
        editor_set_status_message("Bad delim: {}", d);
        return;
    }
    const auto parts = split_delim(std::string_view(c.args).substr(1), d);
    const std::string& pat = parts[0];
    const std::string rep = parts.size() > 1 ? parts[1] : "";
    const std::string flags = parts.size() > 2 ? parts[2] : "";
    if(pat.empty()) {
        editor_set_status_message("Empty pattern");
        return;
    }
    bool global = false, icase = false;
    for(const char f : flags) {
        if(f == 'g') global = true;
        else if(f == 'i') icase = true;
        else if(f == 'I') icase = false;
        else {
            editor_set_status_message("Unknown Flag: {}", f);
            return;
        }
    }
    auto syntax = std::regex::ECMAScript;
    if(icase) syntax |= std::regex::icase;
    std::regex re;
    try {
        re = std::regex(pat, syntax);
    } catch(const std::regex_error&) {
        editor_set_status_message("Invalid pattern: {}", pat);
        return;
    }
    const std::string fmt = to_format(rep);
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

using CmdFn = std::function<void(const CmdLine&)>;
std::map<std::string, CmdFn, std::less<>>& table() {
    static std::map<std::string, CmdFn, std::less<>> t;
    return t;
}

void def(std::initializer_list<const char*> names, CmdFn fn) {
    for(const char* n : names) table()[n] = fn;
}

}  // namespace

void register_builtin_commands() {
    def({"q", "quit"}, [](const CmdLine& c) {
        if(!c.bang && quit_blocked()) return;
        editor_quit();
    });
    def({"w", "write"}, [](const CmdLine&) { editor_save(); });
    def({"wq", "x"}, [](const CmdLine& c) {
        editor_save();
        if(c.bang || !quit_blocked()) editor_quit();
    });
    def({"e", "edit"}, [](const CmdLine& c) {
        if(!c.args.empty()) buffer_open(c.args);
    });
    def({"ls", "buffers"},
        [](const CmdLine&) { editor_set_status_message("{}", buffer_list()); });
    def({"bn", "bnext"}, [](const CmdLine&) { buffer_cycle(+1); });
    def({"bp", "bprev"}, [](const CmdLine&) { buffer_cycle(-1); });
    def({"bd", "bdelete"},
        [](const CmdLine& c) { buffer_close(E.cur, c.bang); });
    def({"b", "buffer"}, [](const CmdLine& c) { buffer_goto(c.args); });
    def({"set"}, [](const CmdLine& c) {
        const std::string m = apply_set_command(c.args);
        if(!m.empty()) editor_set_status_message("{}", m);
    });
    def({"lua"}, [](const CmdLine& c) { script_eval(c.args); });
    def({"m", "move"}, cmd_move);
    def({"s", "substitute"}, cmd_substitute);
    def({"$"}, cmd_shell);
    def({"TSInstall"}, cmd_tsinstall);
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
    const auto it = table().find(c.name);
    if(it == table().end()) {
        editor_set_status_message("Unknown Command: {}", c.name);
        return;
    }
    it->second(c);
}

void editor_command_prompt() {
    std::string cmd = editor_prompt(
        ":", nullptr, std::exchange(E.cmd_prefill, std::string{}));
    set_mode(NORMAL);
    if(cmd.empty()) return;
    editor_command_palette(cmd);
}
