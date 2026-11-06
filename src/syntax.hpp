#pragma once
#include <tree_sitter/api.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "buffer.hpp"
#include "parser.hpp"
#include "piece.hpp"

class Syntax {
public:
    Syntax();
    ~Syntax();
    Syntax(const Syntax&) = delete;
    Syntax& operator=(const Syntax&) = delete;

    std::string_view lang() const { return _name; }

    static std::string_view detect(std::string_view path);
    bool attach(std::string_view filename);
    // Attach by language name ("cpp", "c++", "rs", ...) instead of a file name.
    bool attach_lang(std::string_view lang);
    // Highlights `code` as a standalone document and returns styled byte ranges
    // (sorted, non-overlapping). Leaves the object detached from any buffer.
    std::optional<std::vector<Span>> highlight_text(std::string_view code);
    void parse(const PieceTable& pt, bool trailing_nl);
    std::string summary() const;

    void on_edit(const TextEdit& e);
    void update(PieceTable& pt, bool trailing_nl);
    const std::vector<Span>& line_spans(size_t ls, size_t len);

    void rainbow_prepare(size_t lo, size_t hi);

    std::optional<int> indent_for(size_t line) const;

    enum class Lit { None, Comment, String };
    struct LitSpan {
        Lit kind = Lit::None;
        size_t start = 0, end = 0;
    };
    LitSpan literal_at(size_t byte) const;
    void restyle();

private:
    using Meta = std::vector<std::pair<std::string, std::string>>;
    struct Pred {
        enum Op {
            Eq,
            Match,
            AnyOf,
            Contains,
            KindEq,
            HasParent,
            HasAncestor
        } op{};
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

    struct Delim {
        size_t byte;
        uint32_t depth;
    };

    struct LangHl {
        TSQuery* q = nullptr;
        std::vector<std::vector<Pred>> preds;
        std::vector<std::optional<Style>> cap_style;
        std::vector<int> prio;
        TSQuery* inj = nullptr;
        std::vector<std::vector<Pred>> inj_preds;
        std::vector<Meta> inj_meta;
        uint64_t rev = ~0ULL;
    };

    struct Layer {
        std::string lang;
        std::vector<TSRange> ranges;
        TSParser* parser = nullptr;
        TSTree* tree = nullptr;
        LangHl* hl = nullptr;
    };

    void cache_erase_range(size_t lo, size_t hi);
    void cache_apply_edit(const TextEdit& e);

    bool use_lang(std::string_view lang);
    void drop_tree();
    void detach();
    bool load_query(const TSLanguage* lang, std::string_view name);
    bool load_indent_query(const TSLanguage* lang, std::string_view name);
    void build_preds(TSQuery* q, std::vector<std::vector<Pred>>& out,
                     std::vector<Meta>* meta);
    void add_pred(TSQuery* q, std::vector<std::vector<Pred>>& out, Meta* meta,
                  uint32_t pattern,
                  const std::vector<const TSQueryPredicateStep*>& s);
    bool eval(const std::vector<std::vector<Pred>>& preds,
              const TSQueryMatch& m) const;
    const std::vector<Span>& base_spans(size_t line_start, size_t line_len);
    void rb_walk(TSTreeCursor& c, size_t d, bool generic);
    void rb_emit(size_t s, size_t e, size_t depth);
    std::vector<Span> compute_spans(size_t ls, size_t len);

    LangHl* hl_for(std::string_view lang);
    void collect(TSNode root, LangHl& hl, size_t depth,
                 std::vector<Layer>& out);
    void add_layer(const std::string& lang, std::vector<TSRange> rs,
                   size_t depth, std::vector<Layer>& out);
    void free_layers();

    static constexpr size_t kSpanCacheCap = 4096;

    bool _nl = true;
    bool _edited = false;
    size_t _rb_lo = 0, _rb_hi = 0;
    std::string _err;
    std::string_view _name{};
    std::string _name_store;  // backs _name for attach_lang
    uint64_t _seen_rev = ~0ULL;
    TSQuery* _query = nullptr;
    TSQueryCursor* _qc = nullptr;
    TSParser* _parser = nullptr;
    TSTree* _tree = nullptr;
    TSQuery* _iquery = nullptr;
    std::vector<Meta> _imeta;
    std::vector<Delim> _rb;
    std::vector<Span> _merged;
    std::vector<std::vector<Pred>> _preds;
    std::vector<std::optional<Style>> _cap_style;
    std::vector<int> _prio;
    std::vector<std::vector<Pred>> _ipreds;
    std::vector<Layer> _layers;
    std::map<size_t, CacheLine> _span_cache;
    const PieceTable* _pt = nullptr;
};
std::optional<Style> syntax_theme(std::string_view capture);

// One-shot highlighting of a code snippet (hover fences). Syntax objects are
// kept per language, so the query compiles once. nullopt when the language is
// unknown, not installed, has no highlight query, or `code` is too large.
std::optional<std::vector<Span>> syntax_highlight(std::string_view lang,
                                                  std::string_view code);
