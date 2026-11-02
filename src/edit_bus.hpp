#pragma once
#include <tree_sitter/api.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include "piece.hpp"

struct Pos16 {
    uint32_t line;
    uint32_t col;
};

struct EditEvent {
    size_t pos;
    size_t old_len;
    size_t new_len;
    TSPoint start_pt, old_end_pt, new_end_pt;
    Pos16 start16, old_end16;
    uint64_t version;
    Origin origin;
    const PieceTable* src = nullptr;
};

namespace prio {
constexpr int treesitter = 10;
constexpr int snippet = 20;
constexpr int decor = 30;
}  // namespace prio

using EditFn = std::function<void(const EditEvent&)>;
using GroupFn = std::function<void()>;

void edit_subscribe(int priority, EditFn fn);
void edit_group_end_subscribe(GroupFn fn);
void edit_defer(std::function<void()> fn);
void edit_emit(const EditEvent& ev);
void edit_group_end();
EditEvent make_event(const PieceTable& pt, const TextEdit& e, Origin o);
