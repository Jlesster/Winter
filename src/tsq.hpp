#pragma once
// Tree-sitter queries for scripts: compile, run, and evaluate the predicates
// (#eq? #match? #any-of? and their not- forms). Knows nothing about the editor.
#include <tree_sitter/api.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Text of a byte range of the document the tree was parsed from.
using TsText = std::function<std::string(size_t start, size_t end)>;

class TsQuery {
public:
    // Null on failure, with err like "3:14: unknown node type near 'foo'".
    static std::unique_ptr<TsQuery> compile(const TSLanguage* lang,
                                            std::string_view source,
                                            std::string& err);
    ~TsQuery();
    TsQuery(const TsQuery&) = delete;
    TsQuery& operator=(const TsQuery&) = delete;

    uint32_t pattern_count() const;
    uint32_t capture_count() const;
    std::string capture_name(uint32_t index) const;

    struct Cap {
        uint32_t index;
        TSNode node;
    };
    struct Match {
        uint32_t pattern;
        std::vector<Cap> caps;
    };
    struct Hit {
        uint32_t pattern;
        Cap cap;
    };
    // 0-based inclusive line limits for the nodes a run may return (-1 = open).
    struct Lines {
        int first = -1, last = -1;
    };

    // Whole matches, in the order tree-sitter finds them.
    std::vector<Match> matches(TSNode root, Lines lines, size_t limit,
                               const TsText& text) const;
    // Single captures in document order (what highlighting wants).
    std::vector<Hit> captures(TSNode root, Lines lines, size_t limit,
                              const TsText& text) const;

private:
    struct Pred {
        enum Op { Eq, Match, AnyOf } op = Eq;
        bool negate = false;
        uint32_t cap = 0;
        bool rhs_is_cap = false;
        uint32_t rhs_cap = 0;
        std::string rhs;
        std::regex re;
        std::vector<std::string> any;
    };
    TsQuery() = default;
    bool eval(uint32_t pattern, const TSQueryMatch& m, const TsText& text) const;
    TSQueryCursor* cursor(TSNode root, Lines lines) const;

    TSQuery* q_ = nullptr;
    std::vector<std::vector<Pred>> preds_;  // per pattern
};
