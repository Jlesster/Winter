#include "quickfix.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <system_error>
#include <utility>

#include "buflist.hpp"
#include "command.hpp"
#include "core.hpp"
#include "fold_buf.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "menu.hpp"
#include "piece.hpp"
#include "jumplist.hpp"

namespace {

namespace fs = std::filesystem;

struct List {
    std::string title;
    std::vector<QfEntry> items;
    long cur = -1;  // last entry jumped to, -1 = none yet
} g_list;

Menu g_menu;

const char* type_name(char t) {
    return t == 'w' ? "warning" : t == 'n' ? "note" : "error";
}

std::string short_path(const std::string& p, const fs::path& cwd) {
    if(cwd.empty()) return p;
    const fs::path rel = fs::path(p).lexically_relative(cwd);
    if(rel.empty() || rel.native().starts_with("..")) return p;
    return rel.string();
}

bool same_file(const std::string& a, const std::string& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}

// first (dir > 0) or last (dir < 0) entry that is not a note, else -1
long edge_entry(int dir) {
    const long n = static_cast<long>(g_list.items.size());
    for(long i = dir > 0 ? 0 : n - 1; i >= 0 && i < n; i += dir)
        if(g_list.items[static_cast<size_t>(i)].type != 'n') return i;
    return -1;
}

void step(int dir) {
    const long n = static_cast<long>(g_list.items.size());
    if(n == 0) {
        editor_set_status_message("No quickfix list");
        return;
    }
    long i = (g_list.cur < 0 && dir < 0) ? n : g_list.cur;
    do {
        i += dir;
        if(i < 0 || i >= n) {
            editor_set_status_message("No more items");
            return;
        }
    } while(g_list.items[static_cast<size_t>(i)].type == 'n');
    qf_jump(static_cast<size_t>(i));
}

bool on_menu_key(int c) {
    switch(c) {
        case '\x1b':
        case 'q': g_menu.close(); return true;
        case '\r': {
            const MenuItem* it = g_menu.selected();
            const size_t idx = it ? it->id : 0;
            const bool have = it != nullptr;
            g_menu.close();
            if(have) qf_jump(idx);
            return true;
        }
        case 'j':
        case ARROW_DOWN:
        case CTRL_KEY('n'): g_menu.move(+1); return true;
        case 'k':
        case ARROW_UP:
        case CTRL_KEY('p'): g_menu.move(-1); return true;
        default: return false;  // the menu is modal: everything else is swallowed
    }
}

void cmd_copen(const CmdLine&) {
    g_menu.close();
    if(g_list.items.empty()) {
        editor_set_status_message("Quickfix list is empty");
        return;
    }
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    std::vector<MenuItem> items;
    items.reserve(g_list.items.size());
    for(size_t i = 0; i < g_list.items.size(); ++i) {
        const QfEntry& e = g_list.items[i];
        std::string loc = short_path(e.file, cwd) + ":" + std::to_string(e.line);
        if(e.col > 0) loc += ":" + std::to_string(e.col);
        const char kind = e.type == 'w' ? 'W' : e.type == 'n' ? 'N' : 'E';
        items.push_back({.label = e.text.empty() ? "(no message)" : e.text,
                         .detail = std::move(loc),
                         .kind = kind,
                         .id = i});
    }
    g_menu.open({.at = OverlaySpec::At::Center,
                 .framed = true,
                 .max_rows = 12,
                 .min_w = 50,
                 .max_w = 110,
                 .on_key = on_menu_key,
                 .modal = true});
    g_menu.set_items(std::move(items));
    if(g_list.cur >= 0) g_menu.select(static_cast<size_t>(g_list.cur));
}

void cmd_cc(const CmdLine& c) {
    if(g_list.items.empty()) {
        editor_set_status_message("No quickfix list");
        return;
    }
    long i = g_list.cur >= 0 ? g_list.cur : std::max(0L, edge_entry(+1));
    if(!c.args.empty()) {
        try {
            i = std::stol(c.args) - 1;
        } catch(...) {
            editor_set_status_message("Usage: cc [number]");
            return;
        }
    }
    if(i < 0 || i >= static_cast<long>(g_list.items.size())) {
        editor_set_status_message("No such entry: {}", c.args);
        return;
    }
    qf_jump(static_cast<size_t>(i));
}

void cmd_edge(int dir) {
    const long i = edge_entry(dir);
    if(i < 0) editor_set_status_message("No quickfix list");
    else qf_jump(static_cast<size_t>(i));
}

// :cbuffer[!] [parser ...]  parse the current buffer as compiler output.
void cmd_cbuffer(const CmdLine& c) {
    std::vector<std::string> names;
    for(size_t i = 0; i < c.args.size();) {
        const size_t j = c.args.find_first_of(" ,", i);
        const size_t e = j == std::string::npos ? c.args.size() : j;
        if(e > i) names.push_back(c.args.substr(i, e - i));
        i = e + 1;
    }
    if(names.empty()) names = {"gcc", "rustc", "go", "generic"};
    ErrChain chain(names);
    const std::string text = E.buf().pt.text();
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);

    qf_begin("cbuffer");
    for(size_t i = 0; i < text.size();) {
        const size_t j = text.find('\n', i);
        const size_t e = j == std::string::npos ? text.size() : j;
        if(auto hit = chain.feed(std::string_view(text).substr(i, e - i))) {
            hit->file = qf_resolve_path(hit->file, cwd);
            qf_add(std::move(*hit));
        }
        i = e + 1;
    }
    const QfCounts n = qf_counts();
    if(qf_size() == 0) {
        editor_set_status_message("cbuffer: no errors found");
        return;
    }
    editor_set_status_message(
        "cbuffer: {} errors, {} warnings", n.errors, n.warnings);
    if(!c.bang) qf_jump_first();
}

}  // namespace

std::string qf_resolve_path(std::string_view file, const fs::path& base) {
    fs::path p(file);
    if(p.is_relative() && !base.empty()) p = base / p;
    return p.lexically_normal().string();
}

void qf_begin(std::string title) {
    g_menu.close();
    g_list = {};
    g_list.title = std::move(title);
}

void qf_add(QfEntry e) {
    if(!g_list.items.empty()) {
        const QfEntry& b = g_list.items.back();
        if(b.type == e.type && b.line == e.line && b.col == e.col &&
           b.file == e.file && b.text == e.text)
            return;
    }
    g_list.items.push_back(std::move(e));
}

void qf_set(std::string title, std::vector<QfEntry> entries) {
    qf_begin(std::move(title));
    for(QfEntry& e : entries) qf_add(std::move(e));
}

size_t qf_size() { return g_list.items.size(); }

QfCounts qf_counts() {
    QfCounts n;
    for(const QfEntry& e : g_list.items) {
        if(e.type == 'w') ++n.warnings;
        else if(e.type == 'n') ++n.notes;
        else ++n.errors;
    }
    return n;
}

bool qf_jump(size_t idx) {
    if(idx >= g_list.items.size()) return false;
    const QfEntry e = g_list.items[idx];
    jump_push();
    buffer_open(e.file);
    // buffer_open stays on the current buffer when the file can't be loaded
    if(E.buf().filename.empty() || !same_file(E.buf().filename, e.file))
        return false;
    g_list.cur = static_cast<long>(idx);

    const PieceTable& pt = E.buf().pt;
    const size_t line = std::min(static_cast<size_t>(std::max(e.line, 1) - 1),
                                 pt.line_count() - 1);
    const size_t ls = pt.line_start(line), le = pt.line_end(line);
    size_t off = ls + std::min(static_cast<size_t>(std::max(e.col, 1) - 1), le - ls);
    if(off >= le && le > ls) off = le - 1;
    while(off > ls && (static_cast<unsigned char>(pt.text(off, 1)[0]) & 0xC0) == 0x80)
        --off;  // don't land inside a UTF-8 sequence
    set_cursor_offset(off);
    fold_reveal(kFoldQuickfix);

    editor_set_status_message("({}/{}) {}: {}",
                              idx + 1,
                              g_list.items.size(),
                              type_name(e.type),
                              e.text);
    return true;
}

bool qf_jump_first() {
    const long i = edge_entry(+1);
    return i >= 0 && qf_jump(static_cast<size_t>(i));
}

void qf_register_commands() {
    const auto both = [](const char* a, const char* b,
                         std::function<void(const CmdLine&)> fn) {
        command_add(a, fn);
        command_add(b, fn);
    };
    both("cn", "cnext", [](const CmdLine&) { step(+1); });
    both("cp", "cprev", [](const CmdLine&) { step(-1); });
    command_add("cc", cmd_cc);
    command_add("cfirst", [](const CmdLine&) { cmd_edge(+1); });
    command_add("clast", [](const CmdLine&) { cmd_edge(-1); });
    command_add("copen", cmd_copen);
    command_add("cclose", [](const CmdLine&) { g_menu.close(); });
    command_add("cbuffer", cmd_cbuffer);
}
