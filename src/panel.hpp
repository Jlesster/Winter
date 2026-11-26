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
