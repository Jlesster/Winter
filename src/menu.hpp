#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "overlay.hpp"

struct MenuItem {
    std::string label;
    std::string detail;      // dim column right of the label
    char kind = 0;           // optional 1-cell tag drawn before the label
    bool disabled = false;   // drawn dim, skipped by move()
    std::vector<size_t> hl;  // ascending byte offsets in label to highlight
    size_t id = 0;           // caller's cookie, never touched by Menu
};

struct MenuSpec {
    OverlaySpec::At at = OverlaySpec::At::Screen;
    int x = 0, y = 0;
    bool up = false;  // At::Screen only: y is the row just below the menu
    bool framed = false;
    bool cover_bars = false;
    bool in_prompt = false;
    int z = 60;
    int max_rows = 8;
    int min_w = 20;  // overall width limits, border included
    int max_w = 80;
    std::function<bool(int)> on_key;
    bool modal = false;
};
// A passive selectable list in an overlay. It never reads keys: the owner
// calls move()/select() from whatever loop owns the keyboard.
class Menu {
public:
    Menu() = default;
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    void open(const MenuSpec& spec);  // "wanted"; shown once it has items
    void close();
    bool is_open() const { return want_; }
    bool visible() const { return id_ != 0; }

    void set_items(std::vector<MenuItem> items);  // resets the selection
    bool move(int delta);  // only the sign counts; wraps, skips disabled
    void select(size_t i);

    size_t size() const { return items_.size(); }
    size_t selected_index() const { return sel_; }
    const MenuItem* selected() const;

private:
    void layout();
    void paint(OverlayId id);

    MenuSpec spec_;
    std::vector<MenuItem> items_;
    size_t sel_ = 0, top_ = 0;
    OverlayId id_ = 0;
    unsigned gen_ = 0;
    bool want_ = false;
    // measured by layout()
    int off_ = 0, lw_ = 0, dw_ = 0, inner_ = 0, rows_ = 0;
    bool kinds_ = false;
};
