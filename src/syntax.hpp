#pragma once
#include <tree_sitter/api.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "buffer.hpp"
#include "piece.hpp"

class Syntax {
public:
    Syntax();
    ~Syntax();
    Syntax(const Syntax&) = delete;
    Syntax& operator=(const Syntax&) = delete;

    bool attach(std::string_view filename);
    void parse(const PieceTable& pt, bool trailing_nl);
    std::string summary() const;

    void update(PieceTable& pt, bool trailing_nl);
    const std::vector<Span>& line_spans(size_t line_start, size_t line_len);

private:
    struct Pred {
        enum Op { Eq, Match, AnyOf } op{};
        bool negate = false;
        uint32_t cap = 0;
        bool rhs_is_cap = 0;
        uint32_t rhs_cap = 0;
        std::string rhs;
        std::regex re;
        std::vector<std::string> any;
    };

    struct CacheLine {
        size_t len;
        std::vector<Span> spans;
    };

    void cache_erase_range(size_t lo, size_t hi);
    void cache_apply_edit(const TextEdit& e);

    void drop_tree();
    bool load_query(const TSLanguage* lang, std::string_view name);
    void add_pred(uint32_t pattern,
                  const std::vector<const TSQueryPredicateStep*>& s);
    bool eval(const TSQueryMatch& m) const;
    std::vector<Span> compute_spans(size_t ls, size_t len);

    bool _nl = true;
    uint64_t _seen_rev = ~0ULL;

    TSQuery* _query = nullptr;
    TSQueryCursor* _qc = nullptr;
    const PieceTable* _pt = nullptr;
    TSParser* _parser = nullptr;
    TSTree* _tree = nullptr;

    static constexpr size_t kSpanCacheCap = 4096;

    std::vector<std::vector<Pred>> _preds;
    std::vector<std::optional<Style>> _cap_style;
    std::map<size_t, CacheLine> _span_cache;
    std::string _err;
    std::string_view _name{};
};
