#include <cstdio>
#include <string>
#include <vector>
#include "tsq.hpp"
extern "C" const TSLanguage* tree_sitter_c();

static int n = 0, bad = 0;
#define CHECK(c) do { ++n; if(!(c)) { ++bad; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static std::string g_src;
static TsText text = [](size_t a, size_t b) { return g_src.substr(a, b - a); };
static std::string nodetext(TSNode nd) { return g_src.substr(ts_node_start_byte(nd), ts_node_end_byte(nd) - ts_node_start_byte(nd)); }

struct Tree { TSParser* p; TSTree* t; Tree(const std::string& s) { g_src = s; p = ts_parser_new(); ts_parser_set_language(p, tree_sitter_c()); t = ts_parser_parse_string(p, nullptr, g_src.data(), g_src.size()); } ~Tree() { ts_tree_delete(t); ts_parser_delete(p); } TSNode root() { return ts_tree_root_node(t); } };

static std::unique_ptr<TsQuery> Q(const char* src, std::string* err = nullptr) {
    std::string e; auto q = TsQuery::compile(tree_sitter_c(), src, e); if(err) *err = e; if(!q) std::printf("  (compile failed: %s)\n", e.c_str()); return q;
}
static std::vector<std::string> names(TsQuery& q, Tree& t, TsQuery::Lines l = {}, size_t lim = 1000) {
    std::vector<std::string> out; for(auto& h : q.captures(t.root(), l, lim, text)) out.push_back(nodetext(h.cap.node)); return out;
}

int main() {
    Tree t("int add(int a, int b) { return a + b; }\n"
           "static void log_msg(const char *s) { puts(s); }\n"
           "int main(void) { int x = add(1, 2); log_msg(\"hi\"); return x; }\n");

    // captures, in document order
    auto q = Q("(function_definition declarator: (function_declarator declarator: (identifier) @name))");
    CHECK(q && q->pattern_count() == 1 && q->capture_count() == 1 && q->capture_name(0) == "name");
    CHECK((names(*q, t) == std::vector<std::string>{"add", "log_msg", "main"}));
    // limit
    CHECK(names(*q, t, {}, 2).size() == 2);
    // line range: 0-based inclusive
    CHECK((names(*q, t, {1, 1}) == std::vector<std::string>{"log_msg"}));
    CHECK((names(*q, t, {1, 2}) == std::vector<std::string>{"log_msg", "main"}));
    CHECK((names(*q, t, {2, -1}) == std::vector<std::string>{"main"}));
    CHECK((names(*q, t, {-1, 0}) == std::vector<std::string>{"add"}));

    // whole matches with several captures
    auto qm = Q("(call_expression function: (identifier) @fn arguments: (argument_list) @args)");
    auto ms = qm->matches(t.root(), {}, 100, text);
    CHECK(ms.size() == 3);
    CHECK(ms[0].caps.size() == 2 && qm->capture_name(ms[0].caps[0].index) == "fn" && nodetext(ms[0].caps[0].node) == "puts");
    CHECK(nodetext(ms[1].caps[1].node) == "(1, 2)");
    CHECK(qm->matches(t.root(), {}, 2, text).size() == 2);   // limit
    CHECK(qm->matches(t.root(), {2, 2}, 100, text).size() == 2);  // line 3 holds add() and log_msg()

    // several patterns: pattern index
    auto q2 = Q("(function_definition) @def (call_expression) @call");
    int defs = 0, calls = 0; for(auto& h : q2->captures(t.root(), {}, 100, text)) { if(h.pattern == 0) ++defs; else ++calls; }
    CHECK(q2->pattern_count() == 2 && defs == 3 && calls == 3);

    // predicates
    CHECK(names(*Q("((identifier) @id (#eq? @id \"add\"))"), t).size() == 2);
    CHECK(names(*Q("((identifier) @id (#not-eq? @id \"add\"))"), t).size() > 5);
    CHECK((names(*Q("((identifier) @id (#match? @id \"^log\"))"), t) == std::vector<std::string>{"log_msg", "log_msg"}));
    CHECK(names(*Q("((identifier) @id (#not-match? @id \"^[a-z]$\"))"), t).size() > 0);
    CHECK(names(*Q("((identifier) @id (#any-of? @id \"add\" \"main\"))"), t).size() == 3);
    {
        auto notany = names(*Q("((identifier) @id (#not-any-of? @id \"add\" \"main\" \"log_msg\" \"puts\"))"), t);
        for(auto& s : notany) CHECK(s != "add" && s != "main" && s != "log_msg" && s != "puts");
        CHECK(!notany.empty());
    }
    // a failing predicate must not leak the match's other captures
    auto qp = Q("(call_expression function: (identifier) @fn arguments: (argument_list) @args (#eq? @fn \"add\"))");
    auto mp = qp->matches(t.root(), {}, 100, text); CHECK(mp.size() == 1 && nodetext(mp[0].caps[1].node) == "(1, 2)");
    auto cp = qp->captures(t.root(), {}, 100, text); CHECK(cp.size() == 2);
    // two predicates on one pattern: both must hold
    CHECK(names(*Q("((identifier) @id (#match? @id \"^l\") (#not-eq? @id \"log_msg\"))"), t).empty());
    // capture against capture
    { const std::string saved = g_src;
      struct Restore { const std::string& s; ~Restore() { g_src = s; } } restore{saved};
      Tree t2("int d = x + x;\nint e = x + y;\n");
      auto qe = Q("(binary_expression left: (_) @l right: (_) @r (#eq? @l @r))");
      CHECK(qe->matches(t2.root(), {}, 10, text).size() == 1);
      auto qn = Q("(binary_expression left: (_) @l right: (_) @r (#not-eq? @l @r))");
      CHECK(qn->matches(t2.root(), {}, 10, text).size() == 1); }
    // directives compile and are ignored
    CHECK(names(*Q("((identifier) @i (#set! priority \"90\"))"), t).size() > 5);

    // errors carry position and the offending word
    std::string err;
    CHECK(!Q("(nonexistent_node) @x", &err) && err.find("1:2") == 0 && err.find("unknown node type") != std::string::npos && err.find("nonexistent_node") != std::string::npos);
    CHECK(!Q("(function_definition", &err) && err.find("syntax error") != std::string::npos);
    CHECK(!Q("(identifier)\n(identifier) @a (nope) @b", &err) && err.find("2:") == 0);   // line number counts newlines
    CHECK(!Q("((identifier) (#eq? @nope \"x\"))", &err) && err.find("unknown capture") != std::string::npos);
    CHECK(!Q("((identifier) @i (#lua-match? @i \"x\"))", &err) && err.find("unsupported predicate #lua-match?") != std::string::npos);
    CHECK(!Q("((identifier) @i (#match? @i \"(\"))", &err) && err.find("bad regex") != std::string::npos);
    CHECK(!Q("((identifier) @i (#eq? @i))", &err) && err.find("#eq?") != std::string::npos);
    CHECK(!Q("(identifier field_x: (identifier))", &err) && err.find("unknown field") != std::string::npos);

    // empty tree / no matches
    CHECK(names(*Q("(preproc_include) @inc"), t).empty());
    std::printf("%d checks, %d failed\n", n, bad);
    return bad != 0;
}
