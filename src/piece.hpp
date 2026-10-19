#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

enum class Src : uint8_t { Orig, Add };

struct Piece {
    Src src{};
    size_t off{};
    size_t len{};
};

struct LineCol {
    size_t line{};
    size_t col{};
};

struct TextEdit {
    size_t start = 0, old_end = 0, new_end = 0;
    LineCol start_pt{}, old_end_pt{}, new_end_pt{};
    bool reset = false;
};

class PieceTable {
public:
    static constexpr size_t npos = static_cast<size_t>(-1);
    class Group {
    public:
        explicit Group(PieceTable& t)
            : _t(t) {
            _t.begin_group();
        }
        ~Group() { _t.end_group(); }
        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;

    private:
        PieceTable& _t;
    };

    explicit PieceTable(std::string_view original = {});

    PieceTable& insert(size_t p, std::string_view text);
    PieceTable& remove(size_t p, size_t n);

    size_t size() const { return len_of(_root); }
    char at(size_t pos) const;
    std::string_view chunk_at(size_t pos) const;
    std::string text() const;
    std::string text(size_t pos, size_t n) const;

    size_t line_count() const { return 1 + lf_of(_root); }
    size_t line_start(size_t line) const;
    size_t line_of(size_t pos) const;
    size_t line_end(size_t line) const;
    std::string line_text(size_t line) const;
    LineCol to_linecol(size_t pos) const;
    size_t from_linecol(size_t line, size_t col) const;

    void for_each_chunk(size_t pos, size_t n,
                        const std::function<void(std::string_view)>& fn) const;

    void begin_group();
    void end_group();
    std::optional<size_t> undo();
    std::optional<size_t> redo();
    bool can_undo() const { return _depth == 0 && _head > 0; }
    bool can_redo() const { return _depth == 0 && _head < _hist.size(); }

    void mark_saved() { _saved_head = _rev_open ? npos : _head; }
    bool modified() const {
        return (_depth > 0 && _rev_open) || _head != _saved_head;
    }
    void set_history_limit(size_t n);

    uint64_t revision() const { return _rev; }
    std::vector<TextEdit> drain_edits() { return std::exchange(_journal, {}); }

    size_t piece_count() const { return count_nodes(_root); }
    size_t arena_size() const { return _n.size(); }

private:
    using Id = uint32_t;
    static constexpr Id NIL = UINT32_MAX;

    struct Node {
        Piece p{};
        Id l = NIL, r = NIL;
        uint64_t prio = 0;
        size_t lf = 0;
        size_t sum_len = 0;
        size_t sum_lf = 0;
    };

    struct Edit {
        size_t pos{};
        std::vector<Piece> removed{};
        std::vector<Piece> inserted{};
    };

    struct Revision {
        std::vector<Edit> edits{};
    };

    size_t len_of(Id t) const { return t == NIL ? 0 : _n[t].sum_len; }
    size_t lf_of(Id t) const { return t == NIL ? 0 : _n[t].sum_lf; }

    size_t count_nodes(Id t) const {
        return t == NIL ? 0 : 1 + count_nodes(_n[t].l) + count_nodes(_n[t].r);
    }

    std::string_view bytes(const Piece& p) const;
    uint64_t next_rand();
    Id make(Piece p);
    void pull(Id t);
    Id merge(Id a, Id b);
    std::pair<Id, Id> split(Id t, size_t pos);

    void release(Id t);
    void gather(Id t, std::vector<Piece>& out) const;
    bool try_expanded(Id t, size_t add_off, size_t n);

    size_t raw_insert(size_t pos, std::string_view text);
    void raw_remove(size_t pos, size_t n, std::vector<Piece>* removed);
    void raw_insert_pieces(size_t pos, const std::vector<Piece>& ps);
    void replace_span(size_t pos, size_t del, const std::vector<Piece>& ins);

    Revision& open_rev();
    void record_insert(size_t pos, size_t add_off, size_t len);

    void collect(Id t, size_t lo, size_t hi, std::string& out) const;
    void chunks(Id t, size_t lo, size_t hi,
                const std::function<void(std::string_view)>& fn) const;
    void trim_history();
    size_t nth_lf(Id t, size_t k) const;
    void log_edit(const TextEdit& e);

    const std::vector<size_t>& lf_index(Src s) const {
        return s == Src::Orig ? _orig_lf : _add_lf;
    }
    size_t count_lf(Src s, size_t off, size_t len) const;

    std::string_view _original{};
    std::string _add{};
    std::vector<size_t> _orig_lf{};
    std::vector<size_t> _add_lf{};
    std::vector<Node> _n{};
    std::vector<Id> _free{};
    Id _root = NIL;
    uint64_t _rng = 0x9E3779B97F4A7C15ULL;
    uint64_t _rev = 0;
    std::vector<Revision> _hist{};
    size_t _head = 0;
    int _depth = 0;
    bool _rev_open = false;
    size_t _saved_head = 0;
    size_t _max_hist = 0;
    std::vector<TextEdit> _journal{};
    static constexpr size_t kJournalCap = 1024;
};
