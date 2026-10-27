#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
#include "piece.hpp"
#include "virt.hpp"

struct Diag {
    size_t start = 0, end = 0;
    int severity = 1;
    std::string msg, source;
};

std::vector<Diag> diag_from_lsp(const PieceTable& pt, const Json& list,
                                bool utf16);
void diag_shift(std::vector<Diag>& v, const TextEdit& e);
void diag_spans(const std::vector<Diag>& v, size_t ls, size_t len,
                std::vector<LineSpan>& out);
const Diag* diag_worst_on_line(const std::vector<Diag>& v, size_t ls,
                               size_t len);
std::string diag_summary(const std::vector<Diag>& v);
uint32_t diag_color(int sev);
const char* diag_name(int sev);
std::string_view diag_first_line(const Diag& d);
void diag_jump(int dir, int count);
