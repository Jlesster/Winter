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
// Output of long-running jobs (builds, tests, runs): one OutputModel per run
// and three presenters of it.
//
//   Bottom  a dock under the text area, full width (lightweight pane)
//   Float   the same pane in a framed overlay
//   Full    a nofile buffer, so every vim motion, search and yank works
//
// Producers: output_begin() -> output_append() as data arrives ->
// output_end(). The model normalises what it is fed: ANSI colour becomes
// style spans, other escape sequences vanish, a bare \r overwrites the line
// (ninja progress), tabs expand. Line k is the k-th '\n'-terminated line of
// the stream, so parsers fed line by line stay in step with it.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "buffer.hpp"  // Style

using OutId = int;

enum class OutMode : int { Bottom = 0, Float = 1, Full = 2 };
enum class OutState : uint8_t { Running, Done, Killed };
enum class OutAuto : uint8_t { Start, Fail };  // when the policy may open it

struct OutRun {
    std::string name;       // "make": history label
    std::string title;      // shown in the dock title / float frame
    bool streamed = false;  // a run, not a build: opens on start
    std::function<void()> kill;
    std::function<void()> rerun;
};

// ---- producer side --------------------------------------------------------
OutId output_begin(OutRun run);
void output_append(OutId id, std::string_view chunk);
// Ends the stream (a partial last line becomes a line). Idempotent.
void output_end(OutId id, OutState st, int code);

size_t output_lines(OutId id);     // every line, a partial one included
size_t output_complete(OutId id);  // lines already ended by '\n' (or by end)
std::string_view output_line(OutId id, size_t i);
// Marks line `i` as a diagnostic: Enter on it jumps to quickfix entry `qf`.
// type is 'e', 'w' or 'n'.
// `primary` false: a context line of that diagnostic (not counted again).
void output_link(OutId id, size_t i, size_t qf, char type, bool primary = true);
// Colours a line without a quickfix entry ('e' failed test, 'n' skipped).
void output_mark(OutId id, size_t i, char type);
void output_progress(OutId id, std::string_view text);  // shown while running
void output_summary(OutId id, std::string_view text);   // the closing line
std::string output_text(OutId id);  // plain text, for the full presenter

// ---- presenters -----------------------------------------------------------
void output_auto(OutId id, OutAuto ev);  // applies opts.output_auto
void output_present(OutId id, OutMode m);
void output_close();  // dock and float
bool output_visible();
void output_focus(bool on);  // opens the pane first when it is hidden
void output_toggle();        // open (unfocused) or close
bool output_focused();
OutId output_current();  // last begun run, 0 = none

// The pane itself (geometry, drawing, keys) lives in the dock host: dock.hpp.
void output_register_commands();
