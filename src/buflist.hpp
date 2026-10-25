#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

void buffer_switch(size_t idx);
size_t buffer_open(const std::string& path);
bool buffer_close(size_t idx, bool force);
void buffer_cycle(int step);
void buffer_goto_alt();
bool buffer_goto(std::string_view arg);
std::optional<size_t> buffer_first_modified();
std::string buffer_name(size_t i);
std::string buffer_list();
void buffer_remember_cursor();
void buffer_show_text(const std::string& name, const std::string& text);
