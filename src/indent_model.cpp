#include "indent_model.hpp"
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include "options.hpp"
#include "piece.hpp"
#include "syntax.hpp"

namespace indent {

namespace {
constexpr std::string_view kProbe = "()[]{}\"'/";

int indent_cols(std::string_view s) {
    int c = 0;
    for(const char ch : s) {
        if(ch == ' ') ++c;
        else if(ch == '\t') c += opt_ts() - c % opt_ts();
        else break;
    }
    return c;
}

std::string mask_line(Syntax& syn, const PieceTable& pt, size_t l,
                      const std::string& text) {
    std::string s = text;
    const size_t base = pt.line_start(l);
    bool seen = false;
    for(size_t i = 0; i < s.size();) {
        const char c = s[i];
        if(c == ' ' || c == '\t') {
            ++i;
            continue;
        }
        const bool probe = !seen || kProbe.contains(c);
        seen = true;
        const auto lit = probe ? syn.literal_at(base + i) : Syntax::LitSpan{};
        if(lit.kind == Syntax::Lit::None) {
            ++i;
            continue;
        }
        const size_t to =
            std::min(s.size(), lit.end > base ? lit.end - base : i + 1);
        const char fill = lit.kind == Syntax::Lit::Comment ? ' ' : '_';
        for(size_t k = i; k < to; ++k) s[k] = fill;
        i = std::max(to, i + 1);
    }
    return s;
}

}  // namespace

void Model::on_edit(const TextEdit& e) {
    _valid = e.reset ? 0 : std::min(_valid, e.start_pt.line + 1);
}

const LineState& Model::state_at(Syntax& syn, const PieceTable& pt,
                                 size_t line) {
    line = std::min(line, pt.line_count() - 1);
    if(_st.size() < line + 1) _st.resize(line + 1);
    if(_valid == 0) {
        _st[0] = LineState{};
        _valid = 1;
    }
    while(_valid <= line) {
        LineState next = _st[_valid - 1];
        scan_line(syn, pt, _valid - 1, next);
        _st[_valid++] = std::move(next);
    }
    return _st[line];
}

void Model::scan_line(Syntax& syn, const PieceTable& pt, size_t line,
                      LineState& s) {
    const std::string raw = pt.line_text(line);
    advance(s,
            raw,
            mask_line(syn, pt, line, raw),
            indent_cols(raw),
            static_cast<uint32_t>(line));
}

std::optional<int> Model::indent_for(Syntax& syn, const PieceTable& pt,
                                     size_t line) {
    const std::string raw = pt.line_text(line);
    const size_t f = raw.find_first_not_of(" \t");
    if(f != std::string::npos && raw[f] == '#') return 0;
    // copy: the peeks below can grow _st
    const LineState s = state_at(syn, pt, line);
    if(s.pp_cont) {
        if(line == 0) return 0;
        const std::string prev = pt.line_text(line - 1);
        const size_t p = prev.find_first_not_of(" \t");
        return p != std::string::npos && prev[p] == '#' ? opt_sw()
                                                        : indent_cols(prev);
    }
    const std::string code = mask_line(syn, pt, line, raw);
    const size_t k = code.find_first_not_of(" \t");
    const std::string_view cur = k == std::string::npos
                                     ? std::string_view{}
                                     : std::string_view(code).substr(k);
    const Cfg cfg{opt_sw(), opts.indent_namespace, opts.indent_case};

    // first word and indent of a later line that sits directly in a frame
    const auto peek_line = [&](size_t l,
                               Kind kind,
                               uint32_t opener,
                               std::string& word) -> std::optional<int> {
        const FramePtr top = state_at(syn, pt, l).frames;
        if(!top || top->kind != kind || top->line != opener)
            return std::nullopt;
        const std::string r = pt.line_text(l);
        const std::string c = mask_line(syn, pt, l, r);
        const size_t q = c.find_first_not_of(" \t");
        if(q == std::string::npos) return std::nullopt;
        size_t e = q;
        while(e < c.size() &&
              (std::isalnum(static_cast<unsigned char>(c[e])) || c[e] == '_'))
            ++e;
        word = c.substr(q, e - q);
        if(word.empty()) word = c.substr(q, 1);
        return indent_cols(r);
    };
    Peek pk;
    pk.next_label = [&]() -> std::optional<int> {
        if(!s.frames) return std::nullopt;
        for(size_t l = line + 1; l < pt.line_count() && l < line + 60; ++l) {
            std::string w;
            const auto v = peek_line(l, Kind::Switch, s.frames->line, w);
            if(v && (w == "case" || w == "default")) return v;
        }
        return std::nullopt;
    };
    pk.next_inner = [&]() -> std::optional<int> {
        if(!s.frames) return std::nullopt;
        for(size_t l = line + 1; l < pt.line_count() && l < line + 50; ++l) {
            std::string w;
            const auto v = peek_line(l, Kind::Scope, s.frames->line, w);
            if(!v) continue;
            return w == "}" ? std::nullopt : v;
        }
        return std::nullopt;
    };
    return column(s, cur, cfg, pk);
}

}  // namespace indent
