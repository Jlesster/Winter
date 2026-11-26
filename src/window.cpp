/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "window.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace win {
namespace {

struct Node {
    enum Kind : uint8_t { Leaf, Row, Col } kind = Leaf;
    std::unique_ptr<Win> w;                   // Leaf
    std::vector<std::unique_ptr<Node>> kids;  // Row / Col
    std::vector<double> weight;               // cells after the last layout
    Node* parent = nullptr;
};

std::unique_ptr<Node> g_root;
Win* g_focus = nullptr;
Win* g_prev = nullptr;
int g_next_id = 1;
std::vector<Win*> g_all;
std::vector<Rect> g_seps;
Rect g_area;

constexpr int kMinW = 4, kMinH = 3;

std::unique_ptr<Node> leaf(std::unique_ptr<Win> w) {
    auto n = std::make_unique<Node>();
    n->w = std::move(w);
    return n;
}

Node* find_leaf(Node* n, const Win* w) {
    if(n->kind == Node::Leaf) return n->w.get() == w ? n : nullptr;
    for(auto& k : n->kids)
        if(Node* r = find_leaf(k.get(), w)) return r;
    return nullptr;
}

size_t index_in_parent(const Node* n) {
    const Node* p = n->parent;
    for(size_t i = 0; i < p->kids.size(); ++i)
        if(p->kids[i].get() == n) return i;
    return 0;
}

void reparent(Node* n) {
    for(auto& k : n->kids) k->parent = n;
}

void collect(const Node* n, std::vector<Win*>& out) {
    if(n->kind == Node::Leaf) {
        out.push_back(n->w.get());
        return;
    }
    for(const auto& k : n->kids) collect(k.get(), out);
}

Win* first_leaf(Node* n) {
    while(n->kind != Node::Leaf) n = n->kids.front().get();
    return n->w.get();
}

double sum_weight(const Node* n) {
    double s = 0;
    for(const double w : n->weight) s += w;
    return s;
}

std::vector<int> distribute(int avail, const std::vector<double>& w,
                            int minsz) {
    const size_t n = w.size();
    double sum = 0;
    for(const double x : w) sum += x;
    std::vector<int> sz(n);
    int total = 0;
    for(size_t i = 0; i < n; ++i) {
        const double share =
            sum > 0 ? w[i] / sum : 1.0 / static_cast<double>(n);
        sz[i] = std::max(minsz, static_cast<int>(std::lround(avail * share)));
        total += sz[i];
    }
    while(total > avail) {  // too big: shave the largest above the minimum
        size_t best = n;
        for(size_t i = 0; i < n; ++i)
            if(sz[i] > minsz && (best == n || sz[i] > sz[best])) best = i;
        if(best == n) break;
        --sz[best];
        --total;
    }
    while(total < avail) {  // too small: the heaviest takes the rest
        size_t best = 0;
        for(size_t i = 1; i < n; ++i)
            if(w[i] > w[best]) best = i;
        ++sz[best];
        ++total;
    }
    return sz;
}

void assign(Node* n, Rect r) {
    if(n->kind == Node::Leaf) {
        n->w->rect = r;
        return;
    }
    const int cnt = static_cast<int>(n->kids.size());
    const bool row = n->kind == Node::Row;
    const int total = row ? r.w : r.h;
    const int avail = std::max(total - (row ? cnt - 1 : 0), cnt);
    const int minsz = std::min(row ? kMinW : kMinH, std::max(avail / cnt, 1));
    const std::vector<int> sz = distribute(avail, n->weight, minsz);
    int pos = row ? r.x : r.y;
    for(int i = 0; i < cnt; ++i) {
        const Rect cr = row ? Rect{pos, r.y, sz[static_cast<size_t>(i)], r.h}
                            : Rect{r.x, pos, r.w, sz[static_cast<size_t>(i)]};
        assign(n->kids[static_cast<size_t>(i)].get(), cr);
        n->weight[static_cast<size_t>(i)] = sz[static_cast<size_t>(i)];
        pos += sz[static_cast<size_t>(i)];
        if(row && i + 1 < cnt) {
            g_seps.push_back({pos, r.y, 1, r.h});
            ++pos;
        }
    }
}

// Collapses a container left with one child into its parent.
void tidy(Node* n) {
    while(n && n->kind != Node::Leaf && n->kids.size() == 1) {
        std::unique_ptr<Node> only = std::move(n->kids.front());
        n->kids.clear();
        n->weight.clear();
        if(only->kind == Node::Leaf) {
            n->kind = Node::Leaf;
            n->w = std::move(only->w);
        } else {
            n->kind = only->kind;
            n->kids = std::move(only->kids);
            n->weight = std::move(only->weight);
            reparent(n);
        }
    }
    // A child of the same kind as its parent is spliced in.
    if(n && n->kind != Node::Leaf) {
        for(size_t i = 0; i < n->kids.size();) {
            Node* k = n->kids[i].get();
            if(k->kind != n->kind) {
                ++i;
                continue;
            }
            const double each =
                n->weight[i] / static_cast<double>(k->kids.size());
            std::vector<std::unique_ptr<Node>> moved = std::move(k->kids);
            n->kids.erase(n->kids.begin() + static_cast<std::ptrdiff_t>(i));
            n->weight.erase(n->weight.begin() + static_cast<std::ptrdiff_t>(i));
            for(size_t j = 0; j < moved.size(); ++j) {
                n->kids.insert(
                    n->kids.begin() + static_cast<std::ptrdiff_t>(i + j),
                    std::move(moved[j]));
                n->weight.insert(
                    n->weight.begin() + static_cast<std::ptrdiff_t>(i + j),
                    each);
            }
            reparent(n);
        }
    }
}

void refresh() {
    g_all.clear();
    if(g_root) collect(g_root.get(), g_all);
}

}  // namespace

void init(Buffer* b) {
    if(g_root) return;
    auto w = std::make_unique<Win>();
    w->id = g_next_id++;
    w->buf = b;
    g_focus = w.get();
    g_root = leaf(std::move(w));
    refresh();
}

bool inited() { return g_root != nullptr; }
Win* focus() { return g_focus; }
Win* previous() { return g_prev; }
const std::vector<Win*>& all() { return g_all; }
size_t count() { return g_all.size(); }
const std::vector<Rect>& separators() { return g_seps; }

void layout(Rect area) {
    if(!g_root) return;
    g_area = area;
    g_seps.clear();
    assign(g_root.get(), area);
    refresh();
}

void set_focus(Win* w) {
    if(!w || w == g_focus) return;
    g_prev = g_focus;
    g_focus = w;
}

Win* split(bool vertical, bool before) {
    if(!g_root || !g_focus) return nullptr;
    Node* l = find_leaf(g_root.get(), g_focus);
    auto nw = std::make_unique<Win>(*g_focus);
    nw->id = g_next_id++;
    Win* made = nw.get();
    const Node::Kind want = vertical ? Node::Row : Node::Col;
    const double half = std::max(
        static_cast<double>(vertical ? g_focus->rect.w : g_focus->rect.h) / 2.0,
        1.0);

    if(l->parent && l->parent->kind == want) {
        Node* p = l->parent;
        const size_t i = index_in_parent(l);
        auto nn = leaf(std::move(nw));
        nn->parent = p;
        p->weight[i] = half;
        p->kids.insert(
            p->kids.begin() + static_cast<std::ptrdiff_t>(before ? i : i + 1),
            std::move(nn));
        p->weight.insert(
            p->weight.begin() + static_cast<std::ptrdiff_t>(before ? i : i + 1),
            half);
    } else {
        // l becomes a container holding the old window and the new one
        auto old = leaf(std::move(l->w));
        auto nn = leaf(std::move(nw));
        old->parent = nn->parent = l;
        l->kind = want;
        l->kids.clear();
        l->weight.clear();
        if(before) {
            l->kids.push_back(std::move(nn));
            l->kids.push_back(std::move(old));
        } else {
            l->kids.push_back(std::move(old));
            l->kids.push_back(std::move(nn));
        }
        l->weight = {half, half};
    }
    refresh();
    set_focus(made);
    return made;
}

Win* close(Win* w) {
    if(!g_root || !w || g_all.size() <= 1) return nullptr;
    Node* l = find_leaf(g_root.get(), w);
    if(!l || !l->parent) return nullptr;
    Node* p = l->parent;
    const size_t i = index_in_parent(l);
    const size_t heir =
        i + 1 < p->kids.size() ? i + 1 : i - 1;  // takes the space
    p->weight[heir] += p->weight[i];
    p->kids.erase(p->kids.begin() + static_cast<std::ptrdiff_t>(i));
    p->weight.erase(p->weight.begin() + static_cast<std::ptrdiff_t>(i));
    Win* next = nullptr;
    if(g_prev && g_prev != w) next = g_prev;
    if(w == g_prev) g_prev = nullptr;
    // tidy may free nodes, so settle the new focus from the survivors
    Win* heir_win = first_leaf(p->kids[heir > i ? i : heir].get());
    Node* up = p;
    while(up) {
        Node* parent = up->parent;
        tidy(up);
        up = parent;
    }
    refresh();
    if(!next || std::ranges::find(g_all, next) == g_all.end()) next = heir_win;
    if(g_focus == w) {
        g_focus = next;
        g_prev = nullptr;
    }
    return next;
}

void only() {
    if(!g_root || !g_focus) return;
    Win keep = *g_focus;
    auto w = std::make_unique<Win>(keep);
    g_focus = w.get();
    g_prev = nullptr;
    g_root = leaf(std::move(w));
    refresh();
}

Win* neighbor(Dir d, int hx, int hy) {
    if(!g_focus) return nullptr;
    const Rect f = g_focus->rect;
    Win* best = nullptr;
    int best_score = -1;
    for(Win* c : g_all) {
        if(c == g_focus) continue;
        const Rect r = c->rect;
        bool adj = false;
        int lo = 0, hi = 0, hint = 0, flo = 0, fhi = 0;
        switch(d) {
            case Dir::Left:
                adj = r.x + r.w + 1 == f.x;
                lo = r.y;
                hi = r.y + r.h;
                flo = f.y;
                fhi = f.y + f.h;
                hint = hy;
                break;
            case Dir::Right:
                adj = f.x + f.w + 1 == r.x;
                lo = r.y;
                hi = r.y + r.h;
                flo = f.y;
                fhi = f.y + f.h;
                hint = hy;
                break;
            case Dir::Up:
                adj = r.y + r.h == f.y;
                lo = r.x;
                hi = r.x + r.w;
                flo = f.x;
                fhi = f.x + f.w;
                hint = hx;
                break;
            case Dir::Down:
                adj = f.y + f.h == r.y;
                lo = r.x;
                hi = r.x + r.w;
                flo = f.x;
                fhi = f.x + f.w;
                hint = hx;
                break;
        }
        const int overlap = std::min(hi, fhi) - std::max(lo, flo);
        if(!adj || overlap <= 0) continue;
        // the neighbour under the cursor wins; otherwise the biggest overlap
        const int score = (hint >= lo && hint < hi ? 1000000 : 0) + overlap;
        if(score > best_score) {
            best_score = score;
            best = c;
        }
    }
    return best;
}

bool swap_next() {
    if(!g_root || !g_focus) return false;
    Node* l = find_leaf(g_root.get(), g_focus);
    if(!l->parent) return false;
    Node* p = l->parent;
    const size_t i = index_in_parent(l);
    const size_t j = i + 1 < p->kids.size() ? i + 1 : i - 1;
    Node* other = p->kids[j].get();
    if(other->kind != Node::Leaf) return false;
    std::swap(l->w, other->w);
    refresh();
    set_focus(l->w.get());  // the position keeps focus, as in vim
    return true;
}

bool rotate(bool fwd) {
    if(!g_root || !g_focus) return false;
    Node* l = find_leaf(g_root.get(), g_focus);
    Node* p = l->parent;
    if(!p) return false;
    for(const auto& k : p->kids)
        if(k->kind != Node::Leaf) return false;
    const size_t n = p->kids.size();
    std::vector<std::unique_ptr<Win>> ws;
    for(auto& k : p->kids) ws.push_back(std::move(k->w));
    if(fwd) std::rotate(ws.rbegin(), ws.rbegin() + 1, ws.rend());
    else std::rotate(ws.begin(), ws.begin() + 1, ws.end());
    Win* keep = g_focus;
    for(size_t i = 0; i < n; ++i) p->kids[i]->w = std::move(ws[i]);
    g_focus = keep;  // the same window, at its new position
    refresh();
    return true;
}

void move_far(Dir d) {
    if(!g_root || !g_focus || g_all.size() < 2) return;
    Win* w = g_focus;
    Node* l = find_leaf(g_root.get(), w);
    auto moving = std::make_unique<Win>(*w);
    Win* made = moving.get();
    const int id = w->id;
    made->id = id;
    Win* prev = g_prev;
    // take the old window out (close() would drop the focus bookkeeping)
    Node* p = l->parent;
    const size_t i = index_in_parent(l);
    const size_t heir = i + 1 < p->kids.size() ? i + 1 : i - 1;
    p->weight[heir] += p->weight[i];
    p->kids.erase(p->kids.begin() + static_cast<std::ptrdiff_t>(i));
    p->weight.erase(p->weight.begin() + static_cast<std::ptrdiff_t>(i));
    for(Node* up = p; up;) {
        Node* parent = up->parent;
        tidy(up);
        up = parent;
    }
    const Node::Kind want =
        (d == Dir::Left || d == Dir::Right) ? Node::Row : Node::Col;
    const bool first = d == Dir::Left || d == Dir::Up;
    const double total = (want == Node::Row ? g_area.w : g_area.h);
    const double share = std::max(
        total / static_cast<double>(g_all.size() > 1 ? g_all.size() : 2), 1.0);
    auto nl = leaf(std::move(moving));
    if(g_root->kind == want) {
        nl->parent = g_root.get();
        const auto at =
            static_cast<std::ptrdiff_t>(first ? 0 : g_root->kids.size());
        g_root->kids.insert(g_root->kids.begin() + at, std::move(nl));
        g_root->weight.insert(g_root->weight.begin() + at, share);
    } else {
        auto top = std::make_unique<Node>();
        top->kind = want;
        std::unique_ptr<Node> old = std::move(g_root);
        old->parent = top.get();
        nl->parent = top.get();
        const double oldw = std::max(total - share, 1.0);
        if(first) {
            top->kids.push_back(std::move(nl));
            top->kids.push_back(std::move(old));
            top->weight = {share, oldw};
        } else {
            top->kids.push_back(std::move(old));
            top->kids.push_back(std::move(nl));
            top->weight = {oldw, share};
        }
        g_root = std::move(top);
    }
    g_focus = made;
    g_prev = prev == w ? nullptr : prev;
    refresh();
}

namespace {
// The container that decides `width` (a Row) or height (a Col) for the
// focused window, and the child of it that holds the window.
bool axis_parent(bool width, Node*& parent, size_t& idx) {
    if(!g_root || !g_focus) return false;
    Node* n = find_leaf(g_root.get(), g_focus);
    const Node::Kind want = width ? Node::Row : Node::Col;
    while(n->parent) {
        if(n->parent->kind == want) {
            parent = n->parent;
            idx = index_in_parent(n);
            return true;
        }
        n = n->parent;
    }
    return false;
}
}  // namespace

void resize(bool width, int delta) {
    Node* p;
    size_t i;
    if(!axis_parent(width, p, i) || delta == 0) return;
    const size_t j = i + 1 < p->kids.size() ? i + 1 : i - 1;  // gives or takes
    const double min = width ? kMinW : kMinH;
    double d = static_cast<double>(delta);
    d = std::min(d, p->weight[j] - min);
    d = std::max(d, min - p->weight[i]);
    p->weight[i] += d;
    p->weight[j] -= d;
    layout(g_area);
}

bool resize_dir(Dir d, int n) {
    const bool width = d == Dir::Left || d == Dir::Right;
    Node* p;
    size_t i;
    if(!axis_parent(width, p, i) || n <= 0) return false;
    const bool has_next = i + 1 < p->kids.size();
    const bool towards_end = d == Dir::Right || d == Dir::Down;
    resize(width, (towards_end == has_next) ? n : -n);
    return true;
}

void equalize() {
    if(!g_root) return;
    const auto reset = [](auto&& self, Node* n) -> void {
        if(n->kind == Node::Leaf) return;
        std::ranges::fill(n->weight, 1.0);
        for(auto& k : n->kids) self(self, k.get());
    };
    reset(reset, g_root.get());
    layout(g_area);
}

void maximize(bool width) {
    Node* p;
    size_t i;
    if(!axis_parent(width, p, i)) return;
    std::ranges::fill(p->weight, 1.0);
    p->weight[i] = 1000.0;
    layout(g_area);
}

std::vector<Win*> showing(const Buffer* b) {
    std::vector<Win*> out;
    for(Win* w : g_all)
        if(w->buf == b) out.push_back(w);
    return out;
}

}  // namespace win
