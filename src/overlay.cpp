#include "overlay.hpp"

#include <algorithm>
#include <vector>

#include "columns.hpp"
#include "utf8.hpp"

namespace {

struct Overlay {
    OverlayId id;
    OverlaySpec spec;
    std::vector<Cell> cells;  // spec.w * spec.h
    uint64_t stamp;
};

std::vector<Overlay> g_ov;
OverlayId g_next = 1;

Overlay* find(OverlayId id) {
    for(Overlay& o : g_ov)
        if(o.id == id) return &o;
    return nullptr;
}

// ids from the topmost down: z descending, later opens first on ties
std::vector<OverlayId> top_down() {
    std::vector<const Overlay*> v;
    for(const Overlay& o : g_ov) v.push_back(&o);
    std::ranges::stable_sort(v, {}, [](const Overlay* o) { return o->spec.z; });
    std::vector<OverlayId> ids;
    for(auto it = v.rbegin(); it != v.rend(); ++it) ids.push_back((*it)->id);
    return ids;
}

}  // namespace

Rect overlay_place(const OverlaySpec& s, const Viewport& v) {
    Rect r{0, 0, std::min(s.w, v.cols), s.h};
    const int limit = s.cover_bars ? v.rows : v.text_rows;
    if(s.at == OverlaySpec::At::Screen) {
        r.x = s.x;
        r.y = s.y;
        return r;
    }
    r.x = v.cur_x + s.x;
    r.y = v.cur_y + s.y;
    // wanted below the cursor but it doesn't fit: flip above the cursor line
    if(s.y > 0 && r.y + r.h > limit && v.cur_y - r.h >= 0) r.y = v.cur_y - r.h;
    r.x = std::clamp(r.x, 0, std::max(0, v.cols - r.w));
    r.y = std::clamp(r.y, 0, std::max(0, limit - r.h));
    return r;
}

OverlayId overlay_open(OverlaySpec spec) {
    spec.w = std::max(spec.w, 1);
    spec.h = std::max(spec.h, 1);
    Overlay o{g_next++, std::move(spec), {}, overlay_stamp()};
    o.cells.assign(static_cast<size_t>(o.spec.w) * o.spec.h, Cell{});
    g_ov.push_back(std::move(o));
    return g_ov.back().id;
}

void overlay_close(OverlayId id) {
    const auto it = std::ranges::find(g_ov, id, &Overlay::id);
    if(it == g_ov.end()) return;
    auto cb = std::move(it->spec.on_close);
    g_ov.erase(it);
    if(cb) cb();  // after removal, so it may open or close others
}

void overlay_close_all() {
    for(const OverlayId id : top_down()) overlay_close(id);
}

bool overlay_any() { return !g_ov.empty(); }

void overlay_clear(OverlayId id, Style bg) {
    if(Overlay* o = find(id))
        o->cells.assign(o->cells.size(), Cell{U' ', 1, bg});
}

int overlay_put(OverlayId id, int x, int y, std::string_view s, Style st) {
    Overlay* o = find(id);
    if(!o || y < 0 || y >= o->spec.h) return x;
    const int W = o->spec.w;
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        i += d.len;
        const int w = cell_width(d.cp, 0);
        if(w == 0) {  // combining mark: rides on the previous cell
            if(x > 0 && x <= W) o->cells[y * W + x - 1].mark = d.cp;
            continue;
        }
        if(x < 0) {
            x += w;
            continue;
        }
        if(x + w > W) break;
        o->cells[y * W + x] = Cell{d.cp, static_cast<uint8_t>(w), st};
        if(w == 2) o->cells[y * W + x + 1] = Cell{U'\0', 0, st};
        x += w;
    }
    return x;
}

bool overlay_key(int c) {
    for(const OverlayId id : top_down()) {
        Overlay* o = find(id);
        if(!o || !o->spec.on_key) continue;
        const auto fn =
            o->spec.on_key;  // copy: the handler may close its own overlay
        if(fn(c)) return true;
    }
    if(c == '\x1b')
        for(const OverlayId id : top_down())
            if(const Overlay* o = find(id); o && o->spec.esc_closes)
                overlay_close(id);
    return false;
}

void overlay_tick() {
    const uint64_t now = overlay_stamp();
    for(const OverlayId id : top_down())
        if(const Overlay* o = find(id);
           o && o->spec.dismiss_on_move && o->stamp != now)
            overlay_close(id);
}

void overlay_draw(Screen& scr, const Viewport& v) {
    std::vector<OverlayId> ids = top_down();
    for(auto it = ids.rbegin(); it != ids.rend(); ++it) {  // bottom first
        const Overlay* o = find(*it);
        if(!o || !o->spec.visible) continue;
        const Rect r = overlay_place(o->spec, v);
        const int limit = o->spec.cover_bars ? v.rows : v.text_rows;
        for(int row = 0; row < r.h; ++row) {
            const int sy = r.y + row;
            if(sy < 0 || sy >= limit || sy >= scr.height()) continue;
            int first = -1, last = -1;
            for(int col = 0; col < r.w; ++col) {
                const int sx = r.x + col;
                if(sx < 0 || sx >= v.cols || sx >= scr.width()) continue;
                Cell c = o->cells[static_cast<size_t>(row) * o->spec.w + col];
                // never leave half of a wide char at the overlay's edge
                const bool right_clipped = sx + 1 >= v.cols || col + 1 >= r.w;
                if((c.width == 2 && right_clipped) ||
                   (c.width == 0 && col == 0) || (c.width == 0 && sx == 0))
                    c = Cell{U' ', 1, c.style};
                scr.at(sx, sy) = c;
                if(first < 0) first = sx;
                last = sx;
            }
            if(first < 0) continue;
            // repair wide chars on screen that our edges cut in half
            if(first > 0 && scr.at(first - 1, sy).width == 2)
                scr.at(first - 1, sy) =
                    Cell{U' ', 1, scr.at(first - 1, sy).style};
            if(last + 1 < scr.width() && scr.at(last + 1, sy).width == 0)
                scr.at(last + 1, sy) =
                    Cell{U' ', 1, scr.at(last + 1, sy).style};
        }
    }
}
