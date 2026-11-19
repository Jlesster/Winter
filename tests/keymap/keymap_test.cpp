#include <cstdio>
#include <string>
#include "registry.hpp"

static int n = 0, bad = 0;
#define CHECK(c) do { ++n; if(!(c)) { ++bad; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)
static KeySeq K(const char* s) { return parse_keys(s); }

int main() {
    Registry r;
    r.map(NORMAL, "gg", "g.top");
    r.map(NORMAL, "gx", "g.x");
    r.map(NORMAL, "gxx", "g.xx");
    r.map(NORMAL, "dd", "g.dd");

    // no buffer: exactly the old behaviour
    CHECK(r.lookup(NORMAL, K("gg")).kind == Lookup::Exact && r.lookup(NORMAL, K("gg")).action == "g.top");
    CHECK(r.lookup(NORMAL, K("g")).kind == Lookup::Prefix);
    CHECK(r.lookup(NORMAL, K("zz")).kind == Lookup::None);
    CHECK(r.lookup(NORMAL, K("gx")).kind == Lookup::Exact && r.lookup(NORMAL, K("gx")).longer);

    // a local map shadows the global one in its own buffer only
    r.map_local(7, NORMAL, K("gg"), "l.top");
    CHECK(r.lookup(NORMAL, K("gg"), 7).action == "l.top");
    CHECK(r.lookup(NORMAL, K("gg"), 8).action == "g.top");
    CHECK(r.lookup(NORMAL, K("gg")).action == "g.top");          // buf 0: global only
    CHECK(r.lookup(NORMAL, K("dd"), 7).action == "g.dd");        // unrelated keys fall through to the global map

    // a local-only key
    r.map_local(7, NORMAL, K("<C-j>"), "l.cj");
    CHECK(r.lookup(NORMAL, K("<C-j>"), 7).kind == Lookup::Exact);
    CHECK(r.lookup(NORMAL, K("<C-j>"), 8).kind == Lookup::None);
    CHECK(r.lookup(NORMAL, K("<C-j>")).kind == Lookup::None);

    // chords across the two layers: a local prefix, a global exact
    r.map_local(7, NORMAL, K("dj"), "l.dj");
    Hit h = r.lookup(NORMAL, K("d"), 7);
    CHECK(h.kind == Lookup::Prefix && h.longer);                  // wait for the second key
    CHECK(r.lookup(NORMAL, K("dd"), 7).action == "g.dd");
    CHECK(r.lookup(NORMAL, K("dj"), 7).action == "l.dj");
    CHECK(r.lookup(NORMAL, K("dj"), 8).kind == Lookup::None);
    // a local exact whose longer sibling is global: the short one still waits
    r.map_local(7, NORMAL, K("gx"), "l.gx");
    h = r.lookup(NORMAL, K("gx"), 7);
    CHECK(h.kind == Lookup::Exact && h.action == "l.gx" && h.longer);
    CHECK(r.lookup(NORMAL, K("gxx"), 7).action == "g.xx");        // global chord still reachable
    // a global exact whose longer sibling is local
    r.map_local(7, NORMAL, K("ddd"), "l.ddd");
    h = r.lookup(NORMAL, K("dd"), 7);
    CHECK(h.kind == Lookup::Exact && h.action == "g.dd" && h.longer);
    CHECK(r.lookup(NORMAL, K("dd"), 8).longer == false);

    // modes are separate
    CHECK(r.lookup(INSERT, K("gg"), 7).kind == Lookup::None);
    r.map_local(7, INSERT, K("jk"), "l.esc");
    CHECK(r.lookup(INSERT, K("jk"), 7).action == "l.esc" && r.lookup(INSERT, K("jk"), 8).kind == Lookup::None);

    // lookup_local sees only the local layer
    CHECK(r.lookup_local(7, NORMAL, K("gg")).action == "l.top" && r.lookup_local(7, NORMAL, K("dd")).kind == Lookup::Prefix);   // only "ddd" is local: not an exact hit
    CHECK(r.lookup_local(99, NORMAL, K("gg")).kind == Lookup::None);

    // overwrite
    r.map_local(7, NORMAL, K("gg"), "l.top2");
    CHECK(r.lookup(NORMAL, K("gg"), 7).action == "l.top2");

    // unmap_local: the global map shows through again
    CHECK(r.unmap_local(7, NORMAL, K("gg")) && r.lookup(NORMAL, K("gg"), 7).action == "g.top");
    CHECK(!r.unmap_local(7, NORMAL, K("gg")) && !r.unmap_local(99, NORMAL, K("gg")));
    CHECK(r.lookup(NORMAL, K("gg")).action == "g.top");          // the global map was never touched

    // listing
    auto g = r.maps(NORMAL); CHECK(g.size() == 4 && g[0].mode == NORMAL);
    auto l = r.local_maps(7);
    CHECK(l.size() == 5);   // normal: <C-j> dj gx ddd; insert: jk
    CHECK(r.local_maps(8).empty());
    // drop: returns one action per map, everything is gone, others untouched
    r.map_local(8, NORMAL, K("q"), "other");
    auto gone = r.drop_local(7);
    CHECK(gone.size() == 5);
    CHECK(r.lookup(NORMAL, K("dj"), 7).kind == Lookup::None && r.lookup(NORMAL, K("gx"), 7).action == "g.x");
    CHECK(r.lookup(NORMAL, K("q"), 8).action == "other");
    CHECK(r.drop_local(7).empty());
    // mapping on buffer 0 or with empty keys does nothing
    r.map_local(0, NORMAL, K("zz"), "x"); r.map_local(9, NORMAL, KeySeq(), "x");
    CHECK(r.local_maps(9).empty() && r.lookup(NORMAL, K("zz")).kind == Lookup::None);
    // a copied registry (registry_snapshot) carries the local layer; restore of an empty one clears it
    Registry snap = r; Registry fresh; r = fresh;
    CHECK(r.lookup(NORMAL, K("q"), 8).kind == Lookup::None && snap.lookup(NORMAL, K("q"), 8).action == "other");

    std::printf("%d checks, %d failed\n", n, bad);
    return bad != 0;
}
