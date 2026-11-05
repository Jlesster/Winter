#pragma once

#include <string>
#include <string_view>

bool script_init();
void script_shutdown();

std::string script_config_dir();
void script_load_config();  // startup: remember the built-ins, run init.lua,
                            // watch it

bool script_source(std::string_view path);  // run one Lua file, nothing reset
bool script_reload();        // reset what the config set up, re-run init.lua
void script_reload_later();  // same, from the event loop (safe from any
                             // callback)

std::string script_eval(std::string_view code);
const std::string& script_last_error();
