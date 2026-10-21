
#pragma once
#include <string>
#include <string_view>

bool script_init();
void script_shutdown();

std::string script_config_dir();
void script_load_config();

std::string script_eval(std::string_view code);
const std::string& script_last_error();
