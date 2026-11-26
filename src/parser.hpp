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
#include <vector>
#include "job.hpp"

struct TSLanguage;

struct ParserInfo {
    std::string_view name, url, subdir, exts, rev = {};
};

struct Meta {
    const ParserInfo* info;
    std::vector<std::string> types;
};

bool parser_known(std::string_view lang);
bool parser_builtin(std::string_view lang);  // compiled in
bool parser_usable(std::string_view lang);   // builtin or installed
std::string parser_data_dir();               // ~/.local/share/winter
bool parser_installed(std::string_view lang);
std::string_view parser_lang_for_ext(std::string_view ext);
std::string_view parser_lang_for_file(std::string_view basename);
const TSLanguage* parser_load(std::string_view lang);
std::string parser_query(std::string_view lang,
                         std::string_view kind = "highlights");
std::string parser_error();
std::string parser_list();

JobId parser_install(std::string_view lang,
                     std::function<void(std::string_view)> progress,
                     std::function<void(bool, std::string)> done);

const std::vector<ParserInfo>& parser_table();

struct UpdateEvents {
    std::function<void(std::string_view lang, std::string_view line)> progress;
    std::function<void(std::string_view lang, bool ok, std::string_view msg)>
        each;
    std::function<void(bool ok, int failed)> done;
};
bool parser_update(std::string_view lang, bool force, UpdateEvents ev);
void parser_refresh(std::function<void(bool ok, std::string msg)> done);
void parser_autorefresh();
