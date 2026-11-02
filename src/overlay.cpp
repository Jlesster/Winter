#include "overlay.hpp"

#include <algorithm>
#include <vector>

#include "columns.hpp"
#include "glyphs.hpp"
#include "theme.hpp"
#include "utf8.hpp"
#include "virt.hpp"

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

void paint_frame(const Overlay& o) {
    const OverlaySpec& s = o.spec;
    if(!s.framed || s.w < 2 || s.h < 2) return;
    const BorderSet& b = border_set(s.border.value_or(opts.border));
    const int W = s.w, H = s.h;
    const Style bst = s.border_style == Style{}
                          ? theme_ui("ui.float.border", Style{})
                          : s.border_style;
    const auto put = [&](int x, int y, std::string_view g) {
        overlay_put(o.id, x, y, g, bst);
    };
    for(int x = 1; x < W - 1; ++x) put(x, 0, b.t), put(x, H - 1, b.b);
    for(int y = 1; y < H - 1; ++y) put(0, y, b.l), put(W - 1, y, b.r);
    put(0, 0, b.lt);
    put(W - 1, 0, b.tr);
    put(0, H - 1, b.bl);
    put(W - 1, H - 1, b.br);
}

}  // namespace

Rect overlay_place(const OverlaySpec& s, const Viewport& v) {
    Rect r{0, 0, std::min(s.w, v.cols), s.h};
    const int limit = s.cover_bars ? v.rows : v.text_rows;
    if(s.at == OverlaySpec::At::Center) {
        r.x = std::max(0, (v.cols - r.w) / 2) + s.x;
        r.y = std::max(0, (limit - r.h) / 3) + s.y;
        return r;
    }
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
    if(spec.modal) spec.dismiss_on_move = false;
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

bool overlay_modal() {
    for(const Overlay& o : g_ov)
        if(o.spec.modal && o.spec.visible) return true;
    return false;
}

void overlay_set_cursor(OverlayId id, int x, int y) {
    if(Overlay* o = find(id)) o->spec.cursor_x = x, o->spec.cursor_y = y;
}

bool overlay_cursor(const Viewport& v, int& sx, int& sy) {
    for(const OverlayId id : top_down()) {
        const Overlay* o = find(id);
        if(!o || !o->spec.visible || o->spec.cursor_x < 0 ||
           o->spec.cursor_y < 0)
            continue;
        const Rect r = overlay_place(o->spec, v);
        if(o->spec.cursor_x >= r.w || o->spec.cursor_y >= r.h) continue;
        sx = r.x + o->spec.cursor_x;
        sy = r.y + o->spec.cursor_y;
        return true;
    }
    return false;
}

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
        if(!o) continue;
        const bool modal = o->spec.modal && o->spec.visible;
        if(o->spec.on_key) {
            const auto fn = o->spec.on_key;
            if(fn(c)) return true;
        }
        if(modal) {
            const Overlay* m = find(id);
            if(!m) return true;
            if(std::ranges::find(m->spec.pass, c) != m->spec.pass.end())
                return false;
            if(c == '\x1b' && m->spec.esc_closes) overlay_close(id);
            return true;
        }
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

void overlay_draw(Screen& scr, const Viewport& v, bool prompt_active) {
    std::vector<OverlayId> ids = top_down();
    const std::optional<Style> flt = theme_override("ui.float");
    for(auto it = ids.rbegin(); it != ids.rend(); ++it) {  // bottom first
        const Overlay* o = find(*it);
        if(!o || !o->spec.visible) continue;
        if(prompt_active && !o->spec.show_in_prompt) continue;
        if(const auto paint = o->spec.on_paint) {
            paint(*it);
            o = find(*it);
            if(!o) continue;
        }
        paint_frame(*o);
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
                if(flt) {
                    Style fs = *flt;
                    style_patch(fs, c.style);
                    c.style = fs;
                }
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
