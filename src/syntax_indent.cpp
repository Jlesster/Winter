#include <tree_sitter/api.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include "options.hpp"
#include "syntax.hpp"

namespace {

enum Cap { Auto, Begin, End, Dedent, Branch, Ignore, Align, Zero, NCap };
using MetaMap = std::unordered_map<std::string, std::string>;
using CapMap = std::unordered_map<const void*, MetaMap>;

std::optional<Cap> cap_kind(std::string_view n) {
    static constexpr std::pair<std::string_view, Cap> t[] = {
        {"indent.auto", Auto},
        {"indent.begin", Begin},
        {"indent.end", End},
        {"indent.dedent", Dedent},
        {"indent.branch", Branch},
        {"indent.ignore", Ignore},
        {"indent.align", Align},
        {"indent.zero", Zero}};
    for(const auto& [k, v] : t)
        if(k == n) return v;
    return std::nullopt;
}

bool blank(const std::string& s) {
    return s.find_first_not_of(" \t") == std::string::npos;
}

size_t lead_bytes(const std::string& s) {
    size_t i = 0;
    while(i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i;
}

}  // namespace

std::optional<int> Syntax::indent_for(size_t line) const {
    if(!_iquery || !_tree || !_pt) return std::nullopt;
    const PieceTable& pt = *_pt;
    const int sw = opts.shift_width;
    const uint32_t L = static_cast<uint32_t>(line);
    const TSNode root = ts_tree_root_node(_tree);
    auto line_str = [&](size_t l) { return pt.line_text(l); };
    auto indent_cols = [&](size_t l) {
        int c = 0;
        for(const char ch : line_str(l)) {
            if(ch == ' ') ++c;
            else if(ch == '\t') c += opts.tab_stop - c % opts.tab_stop;
            else break;
        }
        return c;
    };
    auto node_at = [&](size_t l, size_t col) {
        const TSPoint a{static_cast<uint32_t>(l), static_cast<uint32_t>(col)};
        const TSPoint b{static_cast<uint32_t>(l),
                        static_cast<uint32_t>(col + 1)};
        return ts_node_descendant_for_point_range(root, a, b);
    };
    const std::string cur = line_str(line);
    size_t prev = line;
    if(blank(cur)) {
        bool found = false;
        while(prev-- > 0)
            if(!blank(line_str(prev))) {
                found = true;
                break;
            }
        if(!found) return std::nullopt;
    }
    CapMap caps[NCap];
    {
        const size_t lo = pt.line_start(blank(cur) ? prev : line);
        const size_t hi = pt.line_end(line);
        TSQueryCursor* qc = ts_query_cursor_new();
        ts_query_cursor_set_byte_range(
            qc, static_cast<uint32_t>(lo), static_cast<uint32_t>(hi));
        ts_query_cursor_exec(qc, _iquery, root);
        TSQueryMatch m;
        while(ts_query_cursor_next_match(qc, &m)) {
            if(!eval(_ipreds, m)) continue;
            MetaMap meta;
            for(const auto& [k, v] : _imeta[m.pattern_index]) meta[k] = v;
            for(uint16_t i = 0; i < m.capture_count; ++i) {
                uint32_t n;
                const char* c = ts_query_capture_name_for_id(
                    _iquery, m.captures[i].index, &n);
                if(const auto k = cap_kind({c, n}))
                    caps[*k][m.captures[i].node.id] = meta;
            }
        }
        ts_query_cursor_delete(qc);
    }
    auto has = [&](Cap k, const void* id) { return caps[k].contains(id); };
    TSNode node;
    if(blank(cur)) {
        const std::string pl = line_str(prev);
        const size_t lead = lead_bytes(pl);
        node = node_at(prev, pl.find_last_not_of(" \t"));
        if(std::string_view(ts_node_type(node)).contains("comment")) {
            const TSNode first = node_at(prev, lead);
            if(!ts_node_eq(first, node)) {
                const size_t scol = ts_node_start_point(node).column;
                const size_t l2 = pl.substr(0, scol).find_last_not_of(" \t");
                if(l2 != std::string::npos) node = node_at(prev, l2);
            }
        }
        if(ts_node_child_count(node) == 0) {
            const std::string_view t = ts_node_type(node);
            const std::string_view want = t == "{"   ? "}"
                                          : t == "(" ? ")"
                                          : t == "[" ? "]"
                                                     : "";
            const TSNode par = ts_node_parent(node);
            if(!want.empty() && !ts_node_is_null(par)) {
                const TSNode last =
                    ts_node_child(par, ts_node_child_count(par) - 1);
                if(want != ts_node_type(last) || ts_node_is_missing(last))
                    return indent_cols(prev) + sw;
            }
        }
        if(has(End, node.id)) node = node_at(line, lead_bytes(cur));
    } else {
        node = node_at(line, lead_bytes(cur));
    }
    if(ts_node_is_null(node)) return std::nullopt;
    int indent = 0;
    if(has(Zero, node.id)) return 0;
    std::unordered_map<uint32_t, bool> done;
    for(TSNode n = node; !ts_node_is_null(n); n = ts_node_parent(n)) {
        const void* id = n.id;
        const uint32_t srow = ts_node_start_point(n).row;
        const uint32_t erow = ts_node_end_point(n).row;
        const bool is_begin = has(Begin, id);

        if(!is_begin && !has(Align, id) && has(Auto, id) && srow < L &&
           L <= erow)
            return std::nullopt;
        if(!is_begin && has(Ignore, id) && srow < L && L <= erow) return 0;

        bool processed = false;
        if(!done[srow] &&
           ((has(Branch, id) && srow == L) || (has(Dedent, id) && srow != L))) {
            indent -= sw;
            processed = true;
        }

        const bool should = !done[srow];
        bool in_err = false;
        if(should) {
            const TSNode par = ts_node_parent(n);
            in_err = !ts_node_is_null(par) && ts_node_has_error(par);
        }
        if(should && is_begin) {
            const MetaMap& md = caps[Begin].at(id);
            if((srow != erow || in_err || md.contains("indent.immediate")) &&
               (srow != L || md.contains("indent.start_at_same_line"))) {
                indent += sw;
                processed = true;
            }
        }

        if(in_err && !has(Align, id)) {
            for(uint32_t i = 0, nc = ts_node_child_count(n); i < nc; ++i) {
                const auto it = caps[Align].find(ts_node_child(n, i).id);
                if(it == caps[Align].end()) continue;
                const MetaMap cp = it->second;
                caps[Align][id] = cp;
                break;
            }
        }

        if(should && has(Align, id) && (srow != erow || in_err) && srow != L) {
            const MetaMap md = caps[Align].at(id);
            auto find_delim = [&](const std::string& d)
                -> std::optional<std::pair<TSNode, bool>> {
                for(uint32_t i = 0, nc = ts_node_child_count(n); i < nc; ++i) {
                    const TSNode c = ts_node_child(n, i);
                    if(d != ts_node_type(c)) continue;
                    const std::string ln = line_str(ts_node_start_point(c).row);
                    const size_t ec = ts_node_end_point(c).column;
                    std::string rest = ec < ln.size() ? ln.substr(ec) : "";
                    std::erase_if(rest, [&](char ch) {
                        return std::isspace(static_cast<unsigned char>(ch)) ||
                               d.find(ch) != std::string::npos;
                    });
                    return std::pair{c, rest.empty()};
                }
                return std::nullopt;
            };
            std::optional<TSNode> o_node, c_node;
            bool o_last = false, c_last = false;
            if(const auto it = md.find("indent.open_delimiter");
               it != md.end()) {
                if(const auto r = find_delim(it->second)) {
                    o_node = r->first;
                    o_last = r->second;
                }
            } else o_node = n;
            if(const auto it = md.find("indent.close_delimiter");
               it != md.end()) {
                if(const auto r = find_delim(it->second)) {
                    c_node = r->first;
                    c_last = r->second;
                }
            } else c_node = n;

            if(o_node) {
                const TSPoint op = ts_node_start_point(*o_node);
                std::optional<uint32_t> c_srow;
                if(c_node) c_srow = ts_node_start_point(*c_node).row;
                bool absolute = false;
                if(o_last) {
                    indent += sw;
                    if(c_last && c_srow && *c_srow < L)
                        indent = std::max(indent - sw, 0);
                } else if(c_last && c_srow && op.row != *c_srow &&
                          *c_srow < L) {
                    indent = std::max(indent - sw, 0);
                } else {
                    const auto inc = md.find("indent.increment");
                    indent =
                        static_cast<int>(op.column) +
                        (inc != md.end() ? std::atoi(inc->second.c_str()) : 1);
                    absolute = true;
                }
                if(c_srow && *c_srow != op.row && *c_srow == L &&
                   md.contains("indent.avoid_last_matching_next") &&
                   indent <= indent_cols(op.row) + sw)
                    indent += sw;
                processed = true;
                if(absolute) return indent;
            }
        }
        done[srow] = done[srow] || processed;
    }
    return std::max(indent, 0);
}
