// winter.normal / winter.feedkeys: drive the editor with key notation
// ("dd", "<C-w>j", "ihello<Esc>"), like :normal and nvim_feedkeys.
#include <lua.hpp>
#include <string>
#include <vector>

#include "input.hpp"
#include "registry.hpp"
#include "script_internal.hpp"

namespace {

// notation -> key ints; false + err for an impossible key like <C-S-a>
bool to_keys(std::string_view notation, std::vector<int>& out, std::string& err) {
    const char* perr = nullptr;
    const KeySeq ks = parse_keys(notation, &perr);
    if(ks.empty() && perr) {
        err = perr;
        return false;
    }
    out.clear();
    for(const char32_t ch : ks) out.push_back(static_cast<int>(ch));
    return true;
}

int fail(lua_State* L, const std::string& msg) {
    lua_pushnil(L);
    lua_pushlstring(L, msg.data(), msg.size());
    return 2;
}

// winter.normal(keys) -> true | nil, err
// Runs the keys NOW, in the current mode, with the user's keymaps in effect
// (like :normal, not :normal!). A command the keys leave unfinished, or a mode
// they enter (insert, a command line), is ended as if <Esc> was typed. The keys
// are not remembered for `.`, so `.` repeats whatever key started the script.
int l_normal(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    std::vector<int> keys;
    std::string err;
    if(!to_keys(std::string_view(s, n), keys, err)) return fail(L, err);
    if(!editor_inject_keys(keys, /*complete=*/true))
        return fail(L, "keys injected from keys injected from ... are nested too deep");
    lua_pushboolean(L, 1);
    return 1;
}

// winter.feedkeys(keys) -> true | nil, err
// Queues the keys for the next turn of the event loop and treats them exactly
// as typed: remembered for `.`, and a half-typed command stays pending.
int l_feedkeys(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    std::vector<int> keys;
    std::string err;
    if(!to_keys(std::string_view(s, n), keys, err)) return fail(L, err);
    editor_feed_keys(std::move(keys));
    lua_pushboolean(L, 1);
    return 1;
}

const luaL_Reg kKeys[] = {
    {"normal", l_normal}, {"feedkeys", l_feedkeys}, {nullptr, nullptr}};

}  // namespace

void script_open_keys() { script_register(nullptr, kKeys); }
