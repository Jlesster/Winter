#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

using EventHandler = std::function<void(const std::vector<std::string>&)>;
int events_on(std::string name, EventHandler fn);
bool events_off(int id);
void events_emit(std::string_view name,
                 const std::vector<std::string>& args = {});
