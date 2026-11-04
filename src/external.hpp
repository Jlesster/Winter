#pragma once

struct Buffer;

void external_init();                         // once, after loop_init()
void external_watch(Buffer& b);               // buffer has a filename now
void external_unwatch(Buffer& b);             // before the Buffer is destroyed
bool external_reload(Buffer& b, bool force);  // :e / :e!  (true = applied)
