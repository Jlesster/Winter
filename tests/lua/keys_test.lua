local n, fail = 0, 0
local function ok(c, m) n = n + 1; if not c then fail = fail + 1; print("FAIL: " .. m) end end
local function same(a, b) if #a ~= #b then return false end for i = 1, #a do if a[i] ~= b[i] then return false end end return true end
local function show(t) return "{" .. table.concat(t, ",") .. "}" end

-- normal: synchronous, completes unfinished commands
ok(winter.normal("dd") == true, "normal returns true")
local k, complete = last_inj(); ok(same(k, { 100, 100 }) and complete == true, "dd -> d d, complete=true: " .. show(k))
winter.normal("ihello<Esc>"); k = last_inj()
ok(same(k, { 105, 104, 101, 108, 108, 111, 27 }), "ihello<Esc>: " .. show(k))
winter.normal("<C-w>j"); k = last_inj(); ok(same(k, { 0x17, 106 }), "<C-w>j: " .. show(k))
winter.normal("<CR><Tab><BS>"); k = last_inj(); ok(#k == 3 and k[1] == 13 and k[2] == 9, "special keys: " .. show(k))
winter.normal("<lt>"); k = last_inj(); ok(same(k, { 60 }), "<lt> is a literal <: " .. show(k))
winter.normal("<C-Left>"); k = last_inj(); ok(#k == 1 and k[1] > 1000, "modified special key keeps its modifier bits: " .. show(k))
local c0 = counts()
local r, err = winter.normal("<C-S-a>")
ok(r == nil and type(err) == "string" and #err > 0, "impossible key -> nil, err: " .. tostring(err))
ok(counts() == c0, "...and nothing is injected")
ok(winter.normal("") == true, "empty notation is a no-op success")
refuse(true)
r, err = winter.normal("x")
ok(r == nil and err:find("nested", 1, true), "refused injection -> nil, err: " .. tostring(err))
refuse(false)
ok(not pcall(winter.normal), "argument required")

-- feedkeys: asynchronous, typed semantics (no injection)
local inj_before = counts()
ok(winter.feedkeys("jj") == true, "feedkeys returns true")
local _, fed = counts(); ok(fed == 1 and same(last_fed(), { 106, 106 }), "queued, not injected")
ok(counts() == inj_before, "feedkeys does not call the synchronous path")
r, err = winter.feedkeys("<C-S-a>"); ok(r == nil and err, "feedkeys rejects bad notation too")
print(("%d checks, %d failed"):format(n, fail))
os.exit(fail == 0 and 0 or 1)
