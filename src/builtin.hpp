#pragma once
// Registers every built-in action, motion and default key binding.
// Call once at startup, before the first key is read.

void register_builtins();           // calls the two below
void register_builtin_motions();    // h l w b e gg G f t ; , ...
void register_builtin_actions();    // i a o u / : ...
void register_builtin_operators();  // d c y > < x D p P, text objects
