#pragma once
#include <cstdint>

struct Buffer;
void undofile_write(const Buffer& b, uint64_t disk_hash, uint64_t disk_size);
void undofile_load(Buffer& b);
