#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct Buffer;

void buffer_detect_syntax(Buffer& b);
bool load_file(Buffer& b, const std::string& path);
int editor_write_file(const std::string& path, uint64_t* hash = nullptr,
                      uint64_t* size = nullptr);
bool write_all(int fd, std::string_view s);
void editor_save();
