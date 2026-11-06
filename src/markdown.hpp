#pragma once
// Markdown, shared by the hover float and markdown buffers.
//
//   md_blocks()        line classification only (cheap, no inline parsing)
//   md_parse()         blocks + styled/concealable ranges of the source
//   md_float_layout()  float backend: wrapped rows for an overlay (K hover)
//   md_buffer_refresh() buffer backend: decorations on a markdown buffer
//
// The parse layer knows nothing about overlays, buffers or the editor.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "buffer.hpp"  // Style

enum class MdRole : uint8_t {
    Heading,
    Code,       // inline `code`
    CodeBlock,  // a line inside a fence (or the fence itself, as a band)
    CodeFence,  // the ``` line text, always a marker
    Strong,
    Emph,
    Strike,
    Link,     // link text
    LinkUrl,  // the (url) part
    Quote,
    Bullet,
    Checkbox,  // level 1 = checked
    Rule,
};

// A range of the source. `marker` ranges are syntax characters (#, **, `,
// ```, [ ] ( ), >, the bullet) as opposed to the content they delimit, so a
// renderer can dim or (later) conceal them. `fill` ranges are whole-line
// bands: they cover the line and its newline so empty lines get painted too.
struct MdSpan {
    size_t lo = 0, hi = 0;
    MdRole role = MdRole::Code;
    uint8_t level = 0;  // Heading: 1-6. Bullet: 1 = ordered. Checkbox: 1 = done
    bool marker = false;
    bool fill = false;
};

enum class MdBlock : uint8_t {
    Blank,
    Text,
    Heading,
    Rule,
    Fence,     // a ``` line
    CodeLine,  // a line inside a fence
    Quote,
    Bullet,
};

struct MdLine {
    size_t lo = 0, hi = 0;  // the line without its "\n" / "\r"
    size_t tl = 0, th = 0;  // lo..hi without surrounding whitespace
    size_t me = 0;          // end of the block marker (heading #s, bullet, >)
    size_t cl = 0, ch = 0;  // content after the marker(s)
    MdBlock kind = MdBlock::Blank;
    uint8_t level = 0;  // Heading depth, Quote nesting, Bullet: 1 = ordered
};

struct MdDoc {
    std::vector<MdLine> lines;
    std::vector<MdSpan> spans;
};

std::vector<MdLine> md_blocks(std::string_view src);
MdDoc md_parse(std::string_view src);

// Theme groups: md.heading, md.h1..md.h6, md.heading.bg, md.code,
// md.code.block, md.marker, md.strong, md.emph, md.strike, md.link,
// md.link.url, md.quote, md.bullet, md.checkbox.checked,
// md.checkbox.unchecked, md.rule. Every default is the built-in look.
// View-only groups: md.code.lang, md.table.border, md.table.head,
// md.callout.<kind> (note, tip, warning, ...).
Style md_style(MdRole role, int level = 0);
Style md_fill_style(MdRole role, int level = 0);  // bg of a `fill` span

// ---- float backend -------------------------------------------------------

enum class MdTone : uint8_t { Text, Code, Heading, Bullet, Quote };

struct MdSeg {
    std::string text;
    MdTone tone;
    std::optional<Style> style;  // syntax colour, patched over the tone's look
};

struct MdRow {
    std::vector<MdSeg> segs;  // empty and !rule = blank line
    bool rule = false;        // drawn by the frame as a divider
    int width = 0;
};

// Wraps `text` to `w` cells, at most `max` rows.
std::vector<MdRow> md_float_layout(std::string_view text, int w, size_t max);

// ---- buffer backend ------------------------------------------------------

// One replacement of the cursor-away display of a markdown line: the source
// range [lo, hi) of the line (bytes, line-relative) is drawn as `text`.
// Empty text conceals. See md_line_view().
struct MdEdit {
    uint32_t lo = 0, hi = 0;
    std::string text;
    Style st{};         // style of `text`, patched over the line
    bool fill = false;  // repeat `text` out to the view width (rules)
};

struct MdState {  // per buffer, owned by Buffer
    uint64_t rev = ~0ULL, theme = ~0ULL;
    size_t spans = 0;
    bool applied = false;
    bool is_md = false;
    std::string fname;  // what is_md was computed for
    // display edits of line i: edits[line_first[i] .. line_first[i + 1])
    std::vector<uint32_t> line_first;
    std::vector<MdEdit> edits;
};

struct Buffer;
struct SubstView;  // command.hpp
void md_buffer_refresh(Buffer& b);  // call once per frame, before drawing

// Conceal + icons: fills `m.line_first` / `m.edits` from a parsed document.
// `heads` (optional) receives the byte ranges of table header rows.
void md_build_view(std::string_view src, const MdDoc& d, MdState& m,
                   std::vector<std::pair<size_t, size_t>>* heads = nullptr);

// What buffer line `row` looks like with markers concealed and icons drawn
// (render-markdown style). False = draw the line raw: insert and replace
// mode, a non-markdown buffer, rendering off. `width` is the text
// area width, for rules. The result maps offsets like a substitute preview.
bool md_line_view(int row, std::string_view real, int width, SubstView& out);
void md_set_enabled(bool on);
bool md_enabled();
void markdown_register_commands();
