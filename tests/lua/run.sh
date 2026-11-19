#!/bin/sh
# Lua API tests. Needs lua5.4, g++ and the Lua 5.4 headers; run from the repo root:
#   sh tests/lua/run.sh
set -eu
t=tests/lua
src=src
inc=${LUAINC:-/usr/include/lua5.4}
lib=${LUALIB:--llua5.4}
WT=$(mktemp -d)
export WT
trap 'rm -rf "$WT"' EXIT

# 1. the Lua runtime: extract the text that ships inside script_runtime.cpp
sed -n '/R"LUA(/,/)LUA";/p' $src/script_runtime.cpp |
    sed '1s/.*R"LUA(//; $s/)LUA";//' >"$WT/runtime.lua"
lua5.4 $t/runtime_test.lua "$WT/runtime.lua"

# 2. winter.fs against the real std::filesystem code
g++ -std=c++23 -I$src -I"$inc" $t/fs_harness.cpp $src/script_fs.cpp $lib -o "$WT/fs"
"$WT/fs" $t/fs_test.lua

# 3. winter.json against the real Json implementation
g++ -std=c++23 -I$src -I"$inc" $t/json_harness.cpp $src/script_json.cpp \
    $src/lua_json.cpp $src/json.cpp $lib -o "$WT/json"
"$WT/json" $t/json_test.lua

# 4. winter.reg bindings against the real registers code (fake clipboard)
g++ -std=c++23 -I$src -I"$inc" $t/reg_harness.cpp $src/script_reg.cpp $src/reg.cpp \
    $lib -o "$WT/reg"
"$WT/reg" $t/reg_test.lua

# 5. register semantics: numbered shift, small delete, append, black hole, clipboard
g++ -std=c++23 -I$src tests/reg/reg_test.cpp $src/reg.cpp -o "$WT/regsem"
"$WT/regsem"

# 6. marks core against the real PieceTable: anchors follow edits, file marks, ''
g++ -std=c++23 -I$src tests/marks/marks_test.cpp $src/marks.cpp $src/piece.cpp -o "$WT/marks"
"$WT/marks"

# 7. winter.normal / winter.feedkeys against the real key-notation parser
g++ -std=c++23 -I$src -I"$inc" $t/keys_harness.cpp $src/script_keys.cpp $src/registry.cpp \
    $lib -o "$WT/keys"
"$WT/keys" $t/keys_test.lua

# 8. buffer-local keymap layer: shadowing, chords across layers, drop, listing
g++ -std=c++23 -I$src tests/keymap/keymap_test.cpp $src/registry.cpp -o "$WT/keymap"
"$WT/keymap"

# 9. winter.ts and the query core against a REAL grammar. Needs the C grammar's
#    sources (any version whose ABI your libtree-sitter supports):
#      TSC_SRC=subprojects/tree-sitter-c/src sh tests/lua/run.sh
if [ -n "${TSC_SRC:-}" ]; then
    cc -c -I"$TSC_SRC" "$TSC_SRC/parser.c" -o "$WT/c_parser.o"
    g++ -std=c++23 -I$src tests/ts/tsq_test.cpp $src/tsq.cpp "$WT/c_parser.o" \
        -ltree-sitter -o "$WT/tsq"
    "$WT/tsq" | grep -v "compile failed"
    g++ -std=c++23 -I$src -I"$inc" $t/ts_harness.cpp $src/script_ts.cpp $src/tsq.cpp \
        "$WT/c_parser.o" -ltree-sitter $lib -o "$WT/tsl"
    "$WT/tsl" $t/ts_test.lua
else
    echo "winter.ts tests skipped: set TSC_SRC to the tree-sitter-c src directory"
fi
