local r = winter.reg
local n, fail = 0, 0
local function ok(c, m) n = n + 1; if not c then fail = fail + 1; print("FAIL: " .. m) end end
ok(r.get() == nil and r.get("a") == nil, "empty registers are nil")
ok(r.set("a", "hello") == true, "set returns true")
local t, lw = r.get("a"); ok(t == "hello" and lw == false, "get text, linewise")
r.set("a", "x\n", true); local t2, lw2 = r.get("a"); ok(t2 == "x\n" and lw2 == true, "linewise flag round-trips")
r.set("A", "tail", false); ok(r.get("a") == "x\ntail\n", "uppercase appends (linewise wins)")
r.set('"', "u"); ok(r.get() == "u" and r.get('"') == "u", "unnamed is the default name")
ok(r.set("%", "no") == false, "read-only register: set returns false")
ok(r.set("?", "no") == false and r.get("?") == nil, "unknown register")
local names = r.list(); local s = table.concat(names)
ok(s:find('"', 1, true) == 1 and s:find("a", 1, true), "list: unnamed first, then holders: " .. s)
ok(not pcall(r.get, "ab") and not pcall(r.set, "", "x") and not pcall(r.set, "a"), "argument checks")
r.set("_", "gone"); ok(r.get("_") == nil, "black hole stays empty")
print(("%d checks, %d failed"):format(n, fail))
os.exit(fail == 0 and 0 or 1)
