#pragma once
#include <vector>
// input exports, not much but honest work uwu

void editor_process_key();
void editor_handle_key(int c);

// Feed keys (the ints parse_keys makes) from a script.
//  editor_inject_keys: right now, in the current mode, like :normal. Not
//    remembered for `.`. With `complete`, whatever the keys left half done (an
//    operator, a chord, insert mode, a command line they started) is ended as if
//    <Esc> was typed. False when injections nest too deep.
//  editor_feed_keys: on the next turn of the event loop, exactly as if typed
//    (remembered for `.`, may leave a command pending).
bool editor_inject_keys(const std::vector<int>& keys, bool complete);
void editor_feed_keys(std::vector<int> keys);
