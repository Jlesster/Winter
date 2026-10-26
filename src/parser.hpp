#pragma once
#include <functional>
#include <string>
#include <string_view>
#include "job.hpp"

struct TSLanguage;

bool parser_known(std::string_view lang);
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
