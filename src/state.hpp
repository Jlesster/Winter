#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

std::string state_dir();
bool state_append(std::string_view name, std::string_view line);
bool state_rewrite(std::string_view name,
                   const std::vector<std::string>& lines);
std::vector<std::string> state_read_lines(std::string_view name);
uint64_t state_hash(std::string_view s, uint64_t h = 0xcbf29ce484222325ULL);

using HistoryMap = std::map<std::string, std::vector<std::string>>;
HistoryMap state_history_load();
void state_history_append(std::string_view prompt, std::string_view entry);

void cursor_remember(const std::string& file, int line, int col);
bool cursor_recall(const std::string& file, int& line, int& col);
