/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "lsp_reader.hpp"
#include <cctype>
#include <charconv>
#include <cstddef>
#include <string_view>

namespace {
constexpr unsigned long long kMaxBody = 64ull << 20;
constexpr size_t kMaxHeader = 8192;

long long content_length(std::string_view hdr) {
    constexpr std::string_view key = "content-length:";
    for(size_t pos = 0; pos < hdr.size();) {
        size_t eol = hdr.find("\r\n", pos);
        if(eol == std::string_view::npos) eol = hdr.size();
        std::string_view line = hdr.substr(pos, eol - pos);
        pos = eol + 2;
        if(line.size() < key.size()) continue;
        bool match = true;
        for(size_t i = 0; i < key.size(); ++i)
            match = std::tolower(static_cast<unsigned char>(line[i])) == key[i];
        if(!match) continue;
        line.remove_prefix(key.size());
        while(!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix(1);
        while(!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        unsigned long long n = 0;
        const auto r =
            std::from_chars(line.data(), line.data() + line.size(), n);
        if(r.ec != std::errc{} || r.ptr != line.data() + line.size() ||
           n > kMaxBody)
            return -1;
        return static_cast<long long>(n);
    }
    return -1;
}

}  // namespace

bool LspReader::feed(std::string_view data, std::vector<std::string>& out) {
    _buf.append(data);
    size_t pos = 0;
    bool ok = true;
    for(;;) {
        const size_t sep = _buf.find("\r\n\r\n", pos);
        if(sep == std::string::npos) {
            if(_buf.size() - pos > kMaxHeader) ok = false;
            break;
        }
        const long long len =
            content_length(std::string_view(_buf).substr(pos, sep - pos));
        if(len < 0) {
            ok = false;
            break;
        }
        const size_t body = sep + 4;
        if(_buf.size() - body < static_cast<size_t>(len)) break;
        out.emplace_back(_buf, body, static_cast<size_t>(len));
        pos = body + static_cast<size_t>(len);
    }
    if(ok) _buf.erase(0, pos);
    else _buf.clear();
    return ok;
}
