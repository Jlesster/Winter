#include "tsq.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <limits>
#include <map>

namespace {

const char* error_name(TSQueryError e) {
    switch(e) {
        case TSQueryErrorSyntax: return "syntax error";
        case TSQueryErrorNodeType: return "unknown node type";
        case TSQueryErrorField: return "unknown field name";
        case TSQueryErrorCapture: return "unknown capture";
        case TSQueryErrorStructure: return "impossible pattern structure";
        case TSQueryErrorLanguage: return "query does not match this language";
        default: return "error";
    }
}

// the word starting at `off`, to show where the query went wrong
std::string near(std::string_view src, size_t off) {
    if(off >= src.size()) return {};
    size_t e = off;
    while(e < src.size() && !std::isspace(static_cast<unsigned char>(src[e])) &&
          src[e] != '(' && src[e] != ')' && src[e] != '[' && src[e] != ']')
        ++e;
    if(e == off) ++e;
    return std::format(" near '{}'", src.substr(off, e - off));
}

}  // namespace

std::unique_ptr<TsQuery> TsQuery::compile(const TSLanguage* lang,
                                          std::string_view source,
                                          std::string& err) {
    uint32_t off = 0;
    TSQueryError et = TSQueryErrorNone;
    TSQuery* q = ts_query_new(lang, source.data(),
                              static_cast<uint32_t>(source.size()), &off, &et);
    if(!q) {
        size_t line = 1, col = 1;
        for(size_t i = 0; i < off && i < source.size(); ++i) {
            if(source[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        err = std::format("{}:{}: {}{}", line, col, error_name(et), near(source, off));
        return nullptr;
    }
    std::unique_ptr<TsQuery> self(new TsQuery());
    self->q_ = q;
    const uint32_t patterns = ts_query_pattern_count(q);
    self->preds_.resize(patterns);
    for(uint32_t p = 0; p < patterns; ++p) {
        uint32_t n = 0;
        const TSQueryPredicateStep* s = ts_query_predicates_for_pattern(q, p, &n);
        for(uint32_t i = 0; i < n;) {
            uint32_t e = i;
            while(e < n && s[e].type != TSQueryPredicateStepTypeDone) ++e;
            // s[i..e) is one predicate: name, then its arguments
            if(s[i].type != TSQueryPredicateStepTypeString) {
                err = "malformed predicate";
                return nullptr;
            }
            uint32_t len = 0;
            const std::string name =
                ts_query_string_value_for_id(q, s[i].value_id, &len);
            const auto arg_text = [&](uint32_t k) {
                uint32_t l = 0;
                return std::string(ts_query_string_value_for_id(q, s[k].value_id, &l));
            };
            Pred pr;
            bool known = true;
            if(name == "eq?" || name == "not-eq?") {
                pr.op = Pred::Eq;
                pr.negate = name[0] == 'n';
                if(e - i != 3 || s[i + 1].type != TSQueryPredicateStepTypeCapture) {
                    err = std::format("#{} needs a capture and a string or capture", name);
                    return nullptr;
                }
                pr.cap = s[i + 1].value_id;
                if(s[i + 2].type == TSQueryPredicateStepTypeCapture) {
                    pr.rhs_is_cap = true;
                    pr.rhs_cap = s[i + 2].value_id;
                } else {
                    pr.rhs = arg_text(i + 2);
                }
            } else if(name == "match?" || name == "not-match?") {
                pr.op = Pred::Match;
                pr.negate = name[0] == 'n';
                if(e - i != 3 || s[i + 1].type != TSQueryPredicateStepTypeCapture ||
                   s[i + 2].type != TSQueryPredicateStepTypeString) {
                    err = std::format("#{} needs a capture and a regex string", name);
                    return nullptr;
                }
                pr.cap = s[i + 1].value_id;
                try {
                    pr.re = std::regex(arg_text(i + 2));
                } catch(const std::regex_error& ex) {
                    err = std::format("#{}: bad regex: {}", name, ex.what());
                    return nullptr;
                }
            } else if(name == "any-of?" || name == "not-any-of?") {
                pr.op = Pred::AnyOf;
                pr.negate = name[0] == 'n';
                if(e - i < 3 || s[i + 1].type != TSQueryPredicateStepTypeCapture) {
                    err = std::format("#{} needs a capture and strings", name);
                    return nullptr;
                }
                pr.cap = s[i + 1].value_id;
                for(uint32_t k = i + 2; k < e; ++k) pr.any.push_back(arg_text(k));
            } else if(name.ends_with('!')) {
                known = false;  // a directive (#set! ...): nothing to filter on
            } else {
                err = std::format("unsupported predicate #{}", name);
                return nullptr;
            }
            if(known) self->preds_[p].push_back(std::move(pr));
            i = e + 1;
        }
    }
    return self;
}

TsQuery::~TsQuery() {
    if(q_) ts_query_delete(q_);
}

uint32_t TsQuery::pattern_count() const { return ts_query_pattern_count(q_); }
uint32_t TsQuery::capture_count() const { return ts_query_capture_count(q_); }

std::string TsQuery::capture_name(uint32_t index) const {
    uint32_t len = 0;
    const char* s = ts_query_capture_name_for_id(q_, index, &len);
    return std::string(s, len);
}

bool TsQuery::eval(uint32_t pattern, const TSQueryMatch& m,
                   const TsText& text) const {
    if(pattern >= preds_.size()) return true;
    const auto node_text = [&](const TSNode& n) {
        return text(ts_node_start_byte(n), ts_node_end_byte(n));
    };
    for(const Pred& p : preds_[pattern]) {
        std::vector<TSNode> nodes;  // every node captured under p.cap
        for(uint16_t i = 0; i < m.capture_count; ++i)
            if(m.captures[i].index == p.cap) nodes.push_back(m.captures[i].node);
        if(nodes.empty()) continue;  // nothing to test: vacuously true
        bool ok = true;
        switch(p.op) {
            case Pred::Eq: {
                std::string rhs = p.rhs;
                if(p.rhs_is_cap) {
                    const TSNode* other = nullptr;
                    for(uint16_t i = 0; i < m.capture_count; ++i)
                        if(m.captures[i].index == p.rhs_cap) {
                            other = &m.captures[i].node;
                            break;
                        }
                    if(!other) continue;
                    rhs = node_text(*other);
                }
                for(const TSNode& n : nodes)
                    if((node_text(n) == rhs) == p.negate) ok = false;
                break;
            }
            case Pred::Match:
                for(const TSNode& n : nodes)
                    if(std::regex_search(node_text(n), p.re) == p.negate) ok = false;
                break;
            case Pred::AnyOf:
                for(const TSNode& n : nodes) {
                    const std::string t = node_text(n);
                    const bool in = std::ranges::find(p.any, t) != p.any.end();
                    if(in == p.negate) ok = false;
                }
                break;
        }
        if(!ok) return false;
    }
    return true;
}

TSQueryCursor* TsQuery::cursor(TSNode root, Lines lines) const {
    TSQueryCursor* qc = ts_query_cursor_new();
    if(lines.first >= 0 || lines.last >= 0) {
        const uint32_t from = lines.first < 0 ? 0 : static_cast<uint32_t>(lines.first);
        const uint32_t to = lines.last < 0 ? std::numeric_limits<uint32_t>::max()
                                           : static_cast<uint32_t>(lines.last) + 1;
        ts_query_cursor_set_point_range(qc, TSPoint{from, 0}, TSPoint{to, 0});
    }
    ts_query_cursor_exec(qc, q_, root);
    return qc;
}

std::vector<TsQuery::Match> TsQuery::matches(TSNode root, Lines lines,
                                             size_t limit,
                                             const TsText& text) const {
    std::vector<Match> out;
    TSQueryCursor* qc = cursor(root, lines);
    TSQueryMatch m;
    while(out.size() < limit && ts_query_cursor_next_match(qc, &m)) {
        if(!eval(m.pattern_index, m, text)) continue;
        Match r{m.pattern_index, {}};
        for(uint16_t i = 0; i < m.capture_count; ++i)
            r.caps.push_back({m.captures[i].index, m.captures[i].node});
        out.push_back(std::move(r));
    }
    ts_query_cursor_delete(qc);
    return out;
}

std::vector<TsQuery::Hit> TsQuery::captures(TSNode root, Lines lines,
                                            size_t limit,
                                            const TsText& text) const {
    std::vector<Hit> out;
    TSQueryCursor* qc = cursor(root, lines);
    std::map<uint32_t, bool> verdict;  // per match id: do its predicates hold?
    TSQueryMatch m;
    uint32_t ci = 0;
    while(out.size() < limit && ts_query_cursor_next_capture(qc, &m, &ci)) {
        auto it = verdict.find(m.id);
        if(it == verdict.end()) {
            const bool ok = eval(m.pattern_index, m, text);
            it = verdict.emplace(m.id, ok).first;
            if(!ok) ts_query_cursor_remove_match(qc, m.id);
        }
        if(!it->second) continue;
        out.push_back({m.pattern_index, {m.captures[ci].index, m.captures[ci].node}});
    }
    ts_query_cursor_delete(qc);
    return out;
}
