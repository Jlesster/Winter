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

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// One diagnostic. Everything after `text` is optional (0 / empty = unknown).
struct QfEntry {
    std::string file;  // as printed by the tool; the runner makes it absolute
    int line = 0, col = 0;
    char type = 'e';  // e = error, w = warning, n = note
    std::string text;
    int end_line = 0, end_col = 0;     // from a caret underline, when printed
    std::string code;                  // E0308, -Wunused-variable, deprecation
    std::string source;                // the parser that produced it
    std::vector<std::string> context;  // snippet / caret / help lines under it
};

struct ErrTest {
    enum class Status : uint8_t { Pass, Fail, Skip };
    Status status = Status::Pass;
    std::string name;
};

// What a parser makes of one line.
//   Diag      a new diagnostic
//   Context   a line that belongs to the previous diagnostic; when it is a
//             caret line, col/end_col say where (the runner fills what the
//             diagnostic header lacked)
//   Test      one test result
//   Progress  "[12/340] Building ..." (shown while running)
//   Summary   the run's closing line
struct ErrEvent {
    enum class Kind : uint8_t { None, Diag, Context, Test, Progress, Summary };
    Kind kind = Kind::None;
    QfEntry diag;
    std::string text;
    int col = 0, end_col = 0;
    ErrTest test;

    explicit operator bool() const { return kind != Kind::None; }
    static ErrEvent of_diag(QfEntry e) {
        ErrEvent v;
        v.kind = Kind::Diag;
        v.diag = std::move(e);
        return v;
    }
    static ErrEvent of_text(Kind k, std::string t) {
        ErrEvent v;
        v.kind = k;
        v.text = std::move(t);
        return v;
    }
    static ErrEvent of_test(ErrTest::Status s, std::string name) {
        ErrEvent v;
        v.kind = Kind::Test;
        v.test = {s, std::move(name)};
        return v;
    }
};

// One instance per run: a parser keeps its own state across lines (rustc
// prints the message on one line and the location on the next).
struct ErrParser {
    virtual ~ErrParser() = default;
    virtual ErrEvent feed(std::string_view line) = 0;
    // End of the stream: a parser holding a half-built diagnostic emits it.
    virtual ErrEvent finish() { return {}; }
};

using ErrParserFactory = std::function<std::unique_ptr<ErrParser>()>;

void errparser_register(std::string name, ErrParserFactory factory);
std::unique_ptr<ErrParser> errparser_make(std::string_view name);
std::vector<std::string> errparser_names();
void errparser_remove(std::string_view name);
// The chain used when a build has no provider of its own (:makeprg).
const std::vector<std::string>& errparser_default_chain();
void errparser_set_default_chain(std::vector<std::string> names);
// gcc javac rustc go kotlin gradle cmake meson ninja ctest meson-test generic
void errparser_register_builtin();

// Several parsers over one stream. Every parser sees every line (so stateful
// ones stay in sync); the first one that produces an event wins.
class ErrChain {
public:
    // Unknown names are skipped; an empty chain falls back to "generic".
    explicit ErrChain(const std::vector<std::string>& names);
    ErrEvent feed(std::string_view line);
    ErrEvent finish();  // call until it returns None

private:
    std::vector<std::unique_ptr<ErrParser>> ps_;
};
