#include <cstdio>
#include <string>
#include <vector>
#include "../src/columns.hpp"
#include "../src/grapheme.hpp"
#include "../src/markdown.hpp"
#include "../src/options.hpp"
#include "../src/syntax.hpp"
#include "../src/theme.hpp"

// ---- stubs for the parts of the editor markdown.cpp links against
Style theme_ui(std::string_view, Style d) { return d; }
std::optional<std::vector<Span>> syntax_highlight(std::string_view,
                                                  std::string_view) {
    return std::nullopt;
}
int opt_ts() { return 4; }

static int fails = 0;
#define CHECK(c)                                                               \
    do {                                                                       \
        if(!(c)) {                                                             \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                   \
            ++fails;                                                           \
        }                                                                      \
    } while(0)

static bool
has_span(const MdDoc& d, MdRole r, bool marker, size_t lo, size_t hi) {
    for(const MdSpan& s : d.spans)
        if(s.role == r && s.marker == marker && s.lo == lo && s.hi == hi)
            return true;
    return false;
}

int main() {
    // ---- column math: cursor stops == clusters, widths == what the terminal
    // draws
    const std::string fam =
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91"
        "\xA7";  // 👨‍👩‍👧
    const std::string line = "a" + fam + "b";
    CHECK(text_width(line) == 1 + 2 + 1);
    CHECK(cx_to_rx(line, 1) == 1);
    CHECK(cx_to_rx(line, 1 + (int)fam.size()) == 3);
    CHECK(snap_cx(line, 5) == 1);   // inside the family -> its start
    CHECK(rx_to_cx(line, 2) == 1);  // right half of a wide cluster
    CHECK(rx_to_cx(line, 3) == 1 + (int)fam.size());  // 'b'
    const std::string acc = "cafe\xCC\x81!";          // café (decomposed) !
    CHECK(text_width(acc) == 5);
    CHECK(snap_cx(acc, 5) == 3);  // inside e + acute -> 'e'
    CHECK(cx_to_rx(acc, 5) == 4);
    // narrow symbols markdown uses a lot must not be counted wide any more
    CHECK(text_width("\xE2\x9C\x93 \xE2\x98\x91 \xE2\x98\x85") == 5);  // ✓ ☑ ★
    CHECK(text_width("\xE2\x9C\x85") == 2);                            // ✅
    CHECK(text_width("\t") == 4);
    CHECK(text_width("a\tb") == 5);

    // ---- inline parse offsets stay byte-exact around multibyte text
    {
        const std::string s =
            "caf\xC3\xA9 **b\xC3\xB6ld** `x`";  // café **böld** `x`
        const MdDoc d = md_parse(s);
        const size_t o = s.find("**");
        CHECK(has_span(d, MdRole::Strong, true, o, o + 2));
        const size_t c = s.find("**", o + 2);
        CHECK(has_span(d, MdRole::Strong, true, c, c + 2));
        CHECK(has_span(d, MdRole::Strong, false, o + 2, c));
        const size_t t = s.find('`');
        CHECK(has_span(d, MdRole::Code, true, t, t + 1));
    }
    // _ intraword rule with non-ASCII letters: snake_café_name is NOT emphasis
    {
        const std::string s = "snake_caf\xC3\xA9_name";
        const MdDoc d = md_parse(s);
        bool emph = false;
        for(const MdSpan& sp : d.spans) emph |= sp.role == MdRole::Emph;
        CHECK(!emph);
        const std::string s2 = "a _real_ one";
        bool e2 = false;
        for(const MdSpan& sp : md_parse(s2).spans)
            e2 |= sp.role == MdRole::Emph;
        CHECK(e2);
    }
    // a NBSP after the opener means it is not left-flanking -> no emphasis
    {
        const std::string s = "*\xC2\xA0x*";
        bool e = false;
        for(const MdSpan& sp : md_parse(s).spans) e |= sp.role == MdRole::Emph;
        CHECK(!e);
    }
    // ---- float layout never splits a cluster and keeps widths honest
    {
        const std::string text =
            "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F"
            "\x91\xA7 hi";
        const auto rows = md_float_layout(text, 2, 10);
        bool ok = !rows.empty();
        for(const MdRow& r : rows) {
            int w = 0;
            for(const MdSeg& sg : r.segs) w += text_width(sg.text);
            ok &= w == r.width && w <= 2;
            for(const MdSeg& sg : r.segs) {  // every segment is whole clusters
                size_t i = 0;
                while(i < sg.text.size()) i = gr_next(sg.text, i);
                ok &= i == sg.text.size();
            }
        }
        CHECK(ok);
        CHECK(rows.size() >= 2);
        CHECK(rows[0].segs.size() == 1 &&
              rows[0].segs[0].text ==
                  "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0"
                  "\x9F\x91\xA7");
    }
    std::printf(fails ? "%d FAILURES\n" : "all markdown/column tests passed\n",
                fails);
    return fails != 0;
}
