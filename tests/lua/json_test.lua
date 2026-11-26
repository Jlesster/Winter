local j = winter.json
local n, fail = 0, 0
local function ok(c, m) n = n + 1; if not c then fail = fail + 1; print("FAIL: " .. m) end end
local v = j.decode('{"a":1,"b":[1,2,3],"c":{"d":"x"},"e":true,"f":null,"g":1.5}')
ok(v.a == 1 and math.type(v.a) == "integer", "integer stays integer")
ok(v.b[3] == 3 and #v.b == 3 and v.c.d == "x" and v.e == true and v.f == nil and v.g == 1.5, "decode values, null -> nil")
local s = j.encode({ 1, 2, { x = "y\n\"q\"" } })
ok(s == '[1,2,{"x":"y\\n\\"q\\""}]', "encode array with escapes: " .. tostring(s))
local rt = j.decode(j.encode({ name = "w", list = { 1, 2 }, nested = { ok = true } }))
ok(rt.name == "w" and rt.list[2] == 2 and rt.nested.ok == true, "round trip")
local bad, err = j.decode("{oops")
ok(bad == nil and type(err) == "string" and err:find("offset"), "decode error -> nil, err: " .. tostring(err))
local x, e2 = j.encode({ f = print })
ok(x == nil and type(e2) == "string", "encode rejects functions: " .. tostring(e2))
ok(j.decode('"héllo"') == "héllo", "utf8 passthrough")
ok(j.decode("[]") ~= nil and #j.decode("[]") == 0, "empty array")
ok(not pcall(j.decode), "decode needs an argument")
print(("%d checks, %d failed"):format(n, fail))
os.exit(fail == 0 and 0 or 1)
