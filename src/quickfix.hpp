#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "errparse.hpp"

struct QfCounts {
    size_t errors = 0, warnings = 0, notes = 0;
};

// One global list. Producers (:make, :cbuffer, later :grep / LSP) call
// qf_begin() and then qf_add() as results arrive, or qf_set() with all of them.
void qf_begin(std::string title);
void qf_add(QfEntry e);  // drops an entry identical to the previous one
void qf_set(std::string title, std::vector<QfEntry> entries);

size_t qf_size();
QfCounts qf_counts();
bool qf_jump(size_t idx);  // opens the file, moves the cursor, reports in the status line
bool qf_jump_first();      // first entry that isn't a note; false if there is none

// `file` as a tool printed it, made absolute against `base` (the directory the
// tool ran in) and normalised. The file may not exist.
std::string qf_resolve_path(std::string_view file, const std::filesystem::path& base);

void qf_register_commands();  // :cn :cp :cc :cfirst :clast :copen :cclose :cbuffer
