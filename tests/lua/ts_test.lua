local ts = winter.ts
local n, fail = 0, 0
local function ok(c, m) n = n + 1; if not c then fail = fail + 1; print("FAIL: " .. m) end end
local function eq(a, b, m) ok(a == b, m .. " (got " .. tostring(a) .. ", want " .. tostring(b) .. ")") end
local function throws(f, pat, m) local s, e = pcall(f); ok(not s and tostring(e):find(pat, 1, true), m .. " (got: " .. tostring(e) .. ")") end

-- buffers without a tree
eq(ts.lang(), "c", "lang of the current buffer"); ok(ts.lang(2) == nil and ts.lang(3) == nil and ts.lang(99) == nil, "lang is nil without a tree")
local r, e = ts.root(2); ok(r == nil and e:find("no language"), "no language: " .. tostring(e))
r, e = ts.root(3); ok(r == nil and e:find("no parser for 'zig'"), "no parser: " .. tostring(e))
r, e = ts.root(99); ok(r == nil and e:find("no such buffer"), "no such buffer: " .. tostring(e))

-- the root and navigation
local root = ts.root()
eq(root:type(), "translation_unit", "root type"); eq(tostring(root), "node(translation_unit)", "tostring")
local l1, c1, l2, c2 = root:range(); eq(table.concat({ l1, c1, l2, c2 }, ","), "1,1,3,1", "root range (1-based, end exclusive)")
local b1, b2 = root:byte_range(); ok(b1 == 0 and b2 == src_len(), "byte range")
ok(root:is_named() and not root:is_missing() and not root:has_error() and not root:is_error() and not root:is_extra(), "flags")
eq(root:buf(), 1, "buf id"); eq(root:lang(), "c", "lang")
eq(root:named_child_count(), 2, "two functions"); eq(#root:named_children(), 2, "named_children"); eq(root:child_count(), 2, "child_count")
local f1, f2 = root:named_child(1), root:child(2)
eq(f1:type(), "function_definition", "first function"); ok(f2:text():find("^static void log_msg"), "text() of the second")
ok(root:child(0) == nil and root:child(3) == nil and root:named_child(9) == nil, "child past the ends is nil")
ok(f1:next_named_sibling() == f2 and f2:prev_named_sibling() == f1 and f1:prev_sibling() == nil and f2:next_sibling() == nil, "siblings (and == compares nodes)")
ok(f1:equals(f1) and not f1:equals(f2) and f1 ~= f2, "equals / ~=")
ok(root:parent() == nil and f1:parent() == root, "parent")
local decl = f1:field("declarator"); eq(#decl, 1, "field() finds the child"); eq(decl[1]:type(), "function_declarator", "...the right one")
eq(#f1:field("no_such_field"), 0, "unknown field is empty")
local kids = f1:children(); ok(#kids == f1:child_count() and kids[1]:type() == "primitive_type", "children() in order")

-- positions
local id = ts.node_at(0, 1, 5); eq(id:type(), "identifier", "node_at finds the identifier"); eq(id:text(), "add", "...its text")
local paren = ts.node_at(0, 1, 8); ok(not paren:is_named() and paren:type() == "(", "an anonymous token by default")
local np = ts.node_at(1, 1, 8, { named = true }); ok(np:is_named(), "named = true skips anonymous tokens: " .. np:type())
local up, depth = id, 0; while up:parent() do up = up:parent(); depth = depth + 1 end
ok(up == root and depth >= 3, "walking parent() reaches the root")
local int = root:descendant(1, 1, 1, 4); eq(int:text(), "int", "descendant(range)"); eq(int:type(), "primitive_type", "...smallest node")
eq(root:descendant(1, 5):type(), "identifier", "descendant(point)")
ok(root:named_descendant(1, 8):is_named(), "named_descendant")
ok(root:sexpr():find("translation_unit", 1, true) and root:sexpr():find("function_definition", 1, true), "sexpr")
eq(id:id(), ts.node_at(0, 1, 5):id(), "id() is stable for the same node"); ok(id:id() ~= f1:id(), "...and differs between nodes")
local seen = {}; seen[id:id()] = true; ok(seen[ts.node_at(0, 1, 5):id()], "id() works as a table key")

-- queries
local q = ts.query("c", "(function_definition declarator: (function_declarator declarator: (identifier) @name))")
eq(q:lang(), "c", "query lang"); eq(q:pattern_count(), 1, "pattern_count"); eq(q:capture_names()[1], "name", "capture_names")
local caps = q:captures(root); eq(#caps, 2, "captures"); eq(caps[1].name, "name", "capture name"); eq(caps[1].node:text(), "add", "capture node"); eq(caps[2].node:text(), "log_msg", "in document order"); eq(caps[1].pattern, 1, "pattern is 1-based")
eq(#q:captures(root, { first = 2, last = 2 }), 1, "line range (1-based inclusive)"); eq(q:captures(root, { first = 2 })[1].node:text(), "log_msg", "open-ended range")
eq(#q:captures(root, { limit = 1 }), 1, "limit")
local qm = ts.query("c", "(call_expression function: (identifier) @fn arguments: (argument_list) @args)")
local ms = qm:matches(root); eq(#ms, 1, "one call"); eq(#ms[1].captures, 2, "two captures"); eq(ms[1].named.fn:text(), "puts", "named.fn"); eq(ms[1].named.args:text(), "(s)", "named.args"); eq(ms[1].pattern, 1, "match pattern")
eq(ms[1].captures[1].name, "fn", "captures keep order")
local qp = ts.query("c", '((identifier) @id (#eq? @id "add"))'); eq(#qp:captures(root), 1, "predicates apply")
eq(#ts.query("c", '((identifier) @id (#match? @id "^log"))'):captures(root), 1, "#match?")
eq(#ts.query("c", '((identifier) @id (#any-of? @id "add" "puts"))'):captures(root), 2, "#any-of?")
-- captured nodes are usable like any other
local first = q:captures(root)[1].node; eq(first:parent():type(), "function_declarator", "captured node navigates")
-- query errors
local bq, berr = ts.query("c", "(bogus_node) @x"); ok(bq == nil and berr:find("unknown node type", 1, true) and berr:find("1:2", 1, true), "compile error with position: " .. tostring(berr))
bq, berr = ts.query("zig", "(x)"); ok(bq == nil and berr:find("no parser for 'zig'", 1, true), "unknown language")
bq, berr = ts.query("c", [[((identifier) @i (#lua-match? @i "x"))]]); ok(bq == nil and berr:find("unsupported predicate", 1, true), "unsupported predicate named")

-- staleness: an edit invalidates every node
local keep = ts.root(); local keepq = q
bump()
throws(function() keep:type() end, "stale node", "a node is stale after the tree changed")
throws(function() keep:text() end, "stale node", "...for text() too (it reads the buffer)")
throws(function() keep:parent() end, "stale node", "...and for navigation")
throws(function() keepq:captures(keep) end, "stale node", "a query refuses a stale node")
eq(tostring(keep), "node(stale)", "tostring never raises")
ok(not keep:equals(ts.root()), "stale and fresh nodes are never equal")
local fresh = ts.root(); eq(fresh:type(), "translation_unit", "a fresh node works"); eq(#q:captures(fresh), 2, "queries survive edits")
set_source("int x = ;\n")   -- a real reparse, and a broken program
throws(function() fresh:type() end, "stale node", "stale after a reparse")
local broken = ts.root(); ok(broken:has_error(), "has_error on broken code")
local found_err; local function walk(nd) if nd:is_error() or nd:is_missing() then found_err = true end for _, c in ipairs(nd:children()) do walk(c) end end
walk(broken); ok(found_err, "an ERROR or MISSING node exists")

-- the same capture name twice in one match: `named` keeps the first, `captures` both
set_source("int d = x + y;\n")
local twice = ts.query("c", "(binary_expression left: (identifier) @id right: (identifier) @id)"):matches(ts.root())
eq(#twice, 1, "one binary expression"); eq(#twice[1].captures, 2, "both captures are listed")
eq(twice[1].named.id:text(), "x", "named keeps the first node of a repeated name"); eq(twice[1].captures[2].node:text(), "y", "...and captures keeps the second")

-- misuse
throws(function() broken.type(q) end, "node expected", "a method applied to the wrong userdata")
throws(function() ts.root("x") end, "number expected", "root wants a number")
-- many queries, collected
for i = 1, 200 do ts.query("c", "(identifier) @i" .. (i % 3 == 0 and "" or "")) end
collectgarbage(); collectgarbage()
ok(#ts.query("c", "(identifier) @i"):captures(ts.root()) >= 1, "still fine after garbage collection")
print(("%d checks, %d failed"):format(n, fail))
os.exit(fail == 0 and 0 or 1)
