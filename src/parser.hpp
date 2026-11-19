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
