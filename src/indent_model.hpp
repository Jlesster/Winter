#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include "piece.hpp"
#include "syntax.hpp"

namespace indent {

enum class Kind : uint8_t { Block, Switch, Scope, Class, Init, Paren, Bracket };
enum class Pending : uint8_t { None, Header, Cont, Label };
enum class HKind : uint8_t { If, Else, Do, Other };
enum class Kw : uint8_t { None, Switch, Scope, Class, Enum, Template, Other };

struct Header {
    std::shared_ptr<const Header> parent;
    int ind;     // actual indent of the header line (if/for/while/else/do)
    HKind kind;  // lets a later `else` find its `if`
};
using HeaderPtr = std::shared_ptr<const Header>;

struct Stmt {
    HeaderPtr headers;  // braceless headers still waiting for their body
    Pending pending = Pending::None;
    Kw kw = Kw::None;         // what the statement's head says it is
    bool paren_seen = false;  // a (...) group was already closed in it
    bool init_list = false;   // past the `:` of a constructor init list
    uint8_t angle = 0;        // open '<' depth inside a head
    uint8_t tern = 0;         // `?` still waiting for its `:`
    int tern_col = -1;  // column of the newest `?` if its line began with `:`
    int init_ind = 0;   // actual indent of line holding :
    int init_col = -1;  // column of the first init after it
    int rhs_col = -1;   // col of first token after '='
    bool after_hdr = false;  // previous token was if/for/while
    char last = 0;           // last token: punct char, 'w' word, '=' return
    uint32_t line = 0;       // first line of the current statement
    int ind = 0;             // actual indent of that line
};

struct Frame {
    std::shared_ptr<const Frame> parent;
    Kind kind;
    char open;           // '{' '(' '['
    uint32_t line;       // line holding the opener
    int owner_ind;       // actual indent of the line that owns the opener
    int align;           // column of first token after the opener, -1 if none
    bool header_paren;   // the ( of an if/for/while
    Stmt outer;          // statement context to restore when this closes
    int case_ind = -1;   // switch: actual indent of its first case
    int first_ind = -1;  // brace: actual indent of its first statement
};
using FramePtr = std::shared_ptr<const Frame>;

struct LineState {
    FramePtr frames;
    Stmt stmt;
    bool pp_cont = false;  // line is a preprocessor continuation
};

void advance(LineState& s, std::string_view raw, std::string_view code, int ind,
             uint32_t line);

struct Cfg {
    int sw = 4;
    int ns = 2;
    int icase = 2;
};

struct Peek {
    std::function<std::optional<int>()> next_label;
    std::function<std::optional<int>()> next_inner;
};

std::optional<int> column(const LineState& s, std::string_view cur,
                          const Cfg& cfg, const Peek& pk);

class Model {
public:
    void on_edit(const TextEdit& e);
    const LineState& state_at(Syntax& syn, const PieceTable& pt, size_t line);
    size_t valid_lines() const { return _valid; }
    std::optional<int> indent_for(Syntax& syn, const PieceTable& pt,
                                  size_t line);

private:
    void scan_line(Syntax& syn, const PieceTable& pt, size_t line,
                   LineState& s);
    std::vector<LineState> _st;  // _st[l] = state at start of line l
    size_t _valid = 0;           // _st[0.._valid) are up to date
};

}  // namespace indent
