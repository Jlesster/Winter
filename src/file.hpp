#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct Buffer;

void buffer_detect_syntax(Buffer& b);
// First line of the buffer (capped), for shebang detection.
std::string buffer_head(const Buffer& b);
// Force a language (:ft). False if there is no usable parser; the buffer
// then keeps its detected one.
bool buffer_set_filetype(Buffer& b, std::string_view lang);
bool load_file(Buffer& b, const std::string& path);
int editor_write_file(Buffer& b, const std::string& path, uint64_t* hash = nullptr,
                      uint64_t* size = nullptr);
bool write_all(int fd, std::string_view s);
void editor_save();
void editor_save_all();
