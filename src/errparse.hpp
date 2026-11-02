#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct QfEntry {
    std::string file;  // as printed by the tool; the runner makes it absolute
    int line = 0, col = 0;
    char type = 'e';  // e = error, w = warning, n = note
    std::string text;
};

// One instance per run: a parser may keep state across lines (rustc prints the
// message on one line and the location on the next).
struct ErrParser {
    virtual ~ErrParser() = default;
    virtual std::optional<QfEntry> feed(std::string_view line) = 0;
};

using ErrParserFactory = std::function<std::unique_ptr<ErrParser>()>;

void errparser_register(std::string name, ErrParserFactory factory);
std::unique_ptr<ErrParser> errparser_make(std::string_view name);
std::vector<std::string> errparser_names();
void errparser_register_builtin();  // gcc, rustc, go, generic

// Several parsers over one stream. Every parser sees every line (so stateful
// ones stay in sync); the first one that produces an entry wins.
class ErrChain {
public:
    // Unknown names are skipped; an empty chain falls back to "generic".
    explicit ErrChain(const std::vector<std::string>& names);
    std::optional<QfEntry> feed(std::string_view line);

private:
    std::vector<std::unique_ptr<ErrParser>> ps_;
};
