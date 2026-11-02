#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace fuzzy {
int rank(std::string_view f, std::string_view q);

struct Match {
    int score = 0;
    std::vector<size_t> pos;
};

bool match(std::string_view text, std::string_view q, Match& out);

}  // namespace fuzzy
