#include "menu.hpp"

#include <algorithm>
#include <utility>

#include "columns.hpp"
#include "editor.hpp"
#include "overlay.hpp"
#include "theme.hpp"
#include "tree.hpp"
#include "utf8.hpp"

namespace {

std::string fit(std::string_view s, int w) {
    std::string out;
    int used = 0;
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        const int cw = cell_width(d.cp, 0);
        if(used + cw > w) break;
        out.append(s.substr(i, d.len));
        used += cw;
        i += d.len;
    }
    return out;
}

}  // namespace

void Menu::open(const MenuSpec& spec) {
    spec_ = spec;
    want_ = true;
    layout();
}

void Menu::close() {
    want_ = false;
    ++gen_;  // the overlay's on_close must not touch us any more
    const OverlayId old = std::exchange(id_, 0);
    if(old) overlay_close(old);
}

void Menu::set_items(std::vector<MenuItem> items) {
    items_ = std::move(items);
    sel_ = top_ = 0;
    for(size_t i = 0; i < items_.size(); ++i)
        if(!items_[i].disabled) {
            sel_ = i;
            break;
        }
    layout();
}

bool Menu::move(int delta) {
    const size_t n = items_.size();
    if(!n) return false;
    size_t i = sel_;
    for(size_t k = 0; k < n; ++k) {
        i = delta < 0 ? (i + n - 1) % n : (i + 1) % n;
        if(!items_[i].disabled) {
            sel_ = i;
            return true;
        }
    }
    return false;
}

void Menu::select(size_t i) {
    if(i < items_.size() && !items_[i].disabled) sel_ = i;
}

const MenuItem* Menu::selected() const {
    return sel_ < items_.size() ? &items_[sel_] : nullptr;
}

// (Re)creates the overlay at the size the current items need. Overlay cells
// are fixed at open time, so any change of size means close + open.
void Menu::layout() {
    const bool reuse = spec_.fixed && want_ && id_ != 0;
    unsigned gen = ++gen_;
    if(!reuse) {
        gen = ++gen_;
        if(const OverlayId old = std::exchange(id_, 0)) overlay_close(old);
    }
    if(!want_ || (items_.empty() && !spec_.fixed)) return;

    int lw = 0, dw = 0;
    kinds_ = false;
    for(const MenuItem& it : items_) {
        lw = std::max(lw, text_width(it.label));
        dw = std::max(dw, text_width(it.detail));
        kinds_ = kinds_ || it.kind != 0;
    }
    off_ = spec_.framed ? 1 : 0;
    const int screen_w = E.term_cols + tree_cols();
    const int cap = std::max(4, std::min(spec_.max_w, screen_w) - 2 * off_);
    dw_ = spec_.fixed ? (dw ? cap / 3 : 0) : std::min(dw, cap / 3);
    const int kind_w = kinds_ ? 2 : 0;
    const int detail_w = dw_ ? 2 + dw_ : 0;
    inner_ = spec_.fixed ? cap
        : std::clamp(1 + kind_w + lw + detail_w + 1,
                     std::min(spec_.min_w, cap),
                     cap);
    lw_ = std::max(1, inner_ - 2 - kind_w - detail_w);
    rows_ = spec_.fixed ? spec_.max_rows
        : std::min<int>(spec_.max_rows,
                        static_cast<int>(items_.size()));
        if(reuse) return;  // same overlay, the next paint shows the new items

    OverlaySpec o;
    o.at = spec_.at;
    o.x = spec_.x;
    o.w = inner_ + 2 * off_;
    o.h = rows_ + 2 * off_;
    o.y = (spec_.at == OverlaySpec::At::Screen && spec_.up) ? spec_.y - o.h
                                                            : spec_.y;
    o.z = spec_.z;
    o.framed = spec_.framed;
    o.cover_bars = spec_.cover_bars;
    o.show_in_prompt = spec_.in_prompt;
    o.on_key = spec_.on_key;
    o.modal = spec_.modal;
    o.dismiss_on_move = false;
    o.esc_closes = false;  // the owner decides when the menu goes away
    o.on_close = [this, gen] {
        if(gen_ == gen) id_ = 0;  // closed behind our back (overlay_close_all)
    };
    o.on_paint = [this](OverlayId id) { paint(id); };
    id_ = overlay_open(std::move(o));
}

void Menu::paint(OverlayId id) {
    overlay_clear(id);
    const size_t rows = static_cast<size_t>(rows_);
    if(sel_ < top_) top_ = sel_;
    else if(sel_ >= top_ + rows) top_ = sel_ - rows + 1;

    const Style base = theme_ui("ui.menu", Style{});
    const Style selected =
        theme_ui("ui.menu.select", Style{.attrs = ATTR_REVERSE});
    const Style kind_st = theme_ui("ui.menu.kind", Style{.attrs = ATTR_BOLD});
    const Style detail_st =
        theme_ui("ui.menu.detail", Style{.attrs = ATTR_DIM});
    const Style off_st = theme_ui("ui.menu.disabled", Style{.attrs = ATTR_DIM});
    const Style match_st =
        theme_ui("ui.menu.match", Style{.attrs = ATTR_BOLD | ATTR_UNDERLINE});

    const int x0 = off_ + 1;
    const int label_x = x0 + (kinds_ ? 2 : 0);
    for(size_t r = 0; r < rows; ++r) {
        const size_t idx = top_ + r;
        const int y = off_ + static_cast<int>(r);
        if(idx >= items_.size()) {  // only a fixed menu has empty rows
            overlay_put(
                id, off_, y, std::string(static_cast<size_t>(inner_), ' '), base);
            continue;
        }
        const MenuItem& it = items_[idx];
        Style row = base;
        if(idx == sel_) row = selected;
        else if(it.disabled) style_patch(row, off_st);

        overlay_put(
            id, off_, y, std::string(static_cast<size_t>(inner_), ' '), row);
        if(kinds_ && it.kind) {
            Style k = row;
            style_patch(k, kind_st);
            overlay_put(id, x0, y, std::string(1, it.kind), k);
        }
        const std::string label = fit(it.label, lw_);
        overlay_put(id, label_x, y, label, row);
        Style m = row;
        style_patch(m, match_st);
        for(const size_t o : it.hl) {
            if(o >= label.size()) continue;
            const size_t len = utf8_decode(label, o).len;
            overlay_put(
                id,
                label_x + text_width(std::string_view(label).substr(0, o)),
                y,
                label.substr(o, len),
                m);
        }
        if(dw_ && !it.detail.empty()) {
            Style d = row;
            style_patch(d, detail_st);
            overlay_put(id, label_x + lw_ + 2, y, fit(it.detail, dw_), d);
        }
    }
}
