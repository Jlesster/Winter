#pragma once
#include <string>
#include <string_view>
#include <vector>

class LspReader {
public:
    bool feed(std::string_view data, std::vector<std::string>& out);

private:
    std::string _buf;
};
