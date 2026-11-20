#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct Buffer;

void buffer_detect_syntax(Buffer& b);
// Start of the buffer (capped at 1 KiB): shebang, modeline and content
// detection.
std::string buffer_head(const Buffer& b);
// Re-run detection on every buffer that is, or wants to be, `lang` (after a
// parser was installed or rebuilt).
void buffer_reattach_syntax(std::string_view lang);
// Force a language (:ft). False if there is no usable parser; the buffer
// then keeps its detected one.
bool buffer_set_filetype(Buffer& b, std::string_view lang);
bool load_file(Buffer& b, const std::string& path);
int editor_write_file(Buffer& b, const std::string& path, uint64_t* hash = nullptr,
                      uint64_t* size = nullptr);
bool write_all(int fd, std::string_view s);
void editor_save();
void editor_save_all();
