#pragma once

#include <memory>
#include "output.hpp"
#include "panel.hpp"

using PanelId = int;
PanelId dock_add(std::unique_ptr<Panel> p);  // host owns it
void dock_remove(PanelId id);
void dock_show(PanelId id, OutMode m);  // Bottom / Float / Full
void dock_switch(PanelId id);           // change the panel, keep the pane
void dock_close();
void dock_focus(bool on);
void dock_toggle();
bool dock_visible();
bool dock_focused();
bool dock_raw();  // focused && active->raw_keys()
bool dock_pinned();
PanelId dock_current();

// Longest prefix of 's' that fits in 'w' cols
std::string_view dock_clip(std::string_view s, int w);

void dock_layout(int text_rows_total);               // was output_layout
int dock_rows();                                     // was output_dock_rows
void dock_paint(const PanelPut& put, int w, int h);  // draws title bar + body
bool dock_cursor(int& sx, int& sy);                  // dock-relative
bool dock_key(int c);                                // was output_key
bool dock_raw_key(int c);  // false unless dock_raw(); true = consumed
bool dock_chord_pending();
void dock_chord_timeout();
