#pragma once
// Ctrl-A / Ctrl-X number bumping. Pure text in, text edit out: no editor
// state, so it is unit-testable on its own (see num_test.cpp).
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace num {

// numformats flags
enum Fmt : unsigned {
    kBin = 1,       // 0b101
    kHex = 2,       // 0xFF
    kOctal = 4,     // 0755, 0o755
    kReal = 8,      // 3.14, 1.5e-3
    kVersion = 16,  // 1.2.3, v2.0.1.4
    kIdent = 32,    // digits glued to identifiers: vec3, utf8
};
inline constexpr unsigned kAll = 63;
inline constexpr unsigned kDefault = kAll;

// "bin,hex,octal"  absolute list ("none" = decimal only, "all")
// "-octal,-ident"  relative to the default (all +/- or all bare, not mixed)
std::optional<unsigned> parse_formats(std::string_view spec,
                                      std::string* err = nullptr);
std::string format_formats(unsigned f);  // "bin,hex,..." (round-trips)

struct Opts {
    unsigned formats = kDefault;
    // Bump only the decimal digit run under (or after) the cursor instead of
    // the whole number: 1.2.3 -> 1.3.3, 3.99 -> 4.00 (fraction run).
    bool int_only = false;
    // A number must start before this column (visual selections).
    size_t max_start = static_cast<size_t>(-1);
};

// Replace line[start, start+len) with text; the cursor goes to `cursor`,
// all byte offsets within the line.
struct Edit {
    size_t start = 0, len = 0;
    std::string text;
    size_t cursor = 0;
};

// First number whose end is past `col` (so: under the cursor, else the next
// one on the line), bumped by delta. nullopt: no number / not representable.
std::optional<Edit> bump(std::string_view line, size_t col, int64_t delta,
                         const Opts& o = {});

// K hover: the integer under the cursor (hex, 0b, 0o, 0755) in every base,
// plus signed / float readings where the width implies them. A 0/1-only run
// of 4+ digits with leading zeros (00000111) reads as binary, with its
// C-octal value on the last row. Empty: the cursor isn't on such a number
// (decimals, reals and versions are skipped).
struct Row {
    std::string label, value;
};
std::vector<Row> describe(std::string_view line, size_t col,
                          unsigned formats = kDefault);

}  // namespace num
