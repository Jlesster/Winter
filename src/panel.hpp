#pragma once
#include <functional>
#include <string>
#include <string_view>
#include "buffer.hpp"  // Style

using PanelPut = std::function<void(int x, int y, std::string_view, Style)>;

class Panel {
public:
    virtual ~Panel() = default;

    virtual std::string title() const = 0;             // left of the title bar
    virtual std::string status() const { return {}; }  // right-aligned in it
    virtual Style title_style() const { return {}; }   // colour of status()

    // Body only, w x h, coordinates relative to the body. The host draws the
    // title bar.
    virtual void paint(const PanelPut& put, int w, int h) = 0;

    virtual bool key(int c) = 0;  // false = not consumed
    virtual bool cursor(int&, int&) const { return false; }  // body-relative
    virtual void resize(int, int) {}     // TIOCSWINSZ later
    virtual void focus_changed(bool) {}  // terminal: focus-in/out reports

    // true = the host hands this panel every key, unfiltered (terminals).
    virtual bool raw_keys() const { return false; }

    // true = other producers must not take the dock from this panel
    // only an explicit :Output does
    virtual bool pinned() const { return false; }
};
