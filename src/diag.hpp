#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
#include "piece.hpp"

struct Diag {
    size_t start = 0, end = 0;
    int severity = 1;
    std::string msg, source;
};

struct DiagMark {
    size_t lo, hi;
    int sev;
};

std::vector<Diag> diag_from_lsp(const PieceTable& pt, const Json& list,
                                bool utf16);
void diag_shift(std::vector<Diag>& v, const TextEdit& e);
void diag_marks(const std::vector<Diag>& v, size_t ls, size_t len,
                std::vector<DiagMark>& out);
const Diag* diag_worst_on_line(const std::vector<Diag>& v, size_t ls,
                               size_t len);
std::string diag_summary(const std::vector<Diag>& v);
uint32_t diag_color(int sev);
const char* diag_name(int sev);
std::string_view diag_first_line(const Diag& d);
void diag_jump(int dir, int count);
