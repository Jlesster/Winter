#pragma once
#include <string_view>
#include "piece.hpp"

struct Buffer;

// Replace the whole buffer text as minimal line hunks (see pt_replace) and
// keep this buffer's cursor on the same line. Does not touch the saved mark.
bool buffer_replace_text(Buffer& b, std::string_view text, Origin o);
