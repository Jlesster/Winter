#pragma once

#include <string>
struct Buffer;

void buffer_detect_syntax(Buffer& b);
bool load_file(Buffer& b, const std::string& path);
int editor_write_file(const std::string& path);
bool write_all(int fd, std::string_view s);
void editor_save();
