#pragma once
#include <lua.hpp>
#include <optional>
#include <string>
#include "json.hpp"

struct lua_State;

bool lua_to_json(lua_State* L, int idx, Json& out, std::string& err);
std::optional<Json> lua_load_config_file(const std::string& path,
                                         std::string& err);
