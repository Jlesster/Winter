#pragma once
#include <string>
#include <string_view>
#include "motion.hpp"
#include "registry.hpp"

void ext_operator(const std::string& name, Operator fn, std::string_view keys);
void ext_motion(const std::string& name, motion::Fn fn, std::string_view keys, bool wants_char);
void ext_object(const std::string& name, ObjectFn fn, std::string_view keys);
