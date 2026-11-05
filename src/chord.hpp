#pragma once

#include <functional>
#include <string>

#include "core.hpp"
#include "registry.hpp"

// Chord state for text-entry modes: insert/replace and the command/search
// prompt. Normal/visual/files keep normal.cpp's state machine.
class Chord {
public:
    enum class Result {
        Pass,      // caller handles this key normally
        Buffered,  // prefix consumed; wait for the next key/timeout
        Handled,   // action ran (or wants_char consumed the key)
    };

    // Replay one pending key as literal text. The caller decides what
    // "literal" means (insert into the buffer, insert into a prompt, ...).
    using Emit = std::function<void(int)>;

    Result feed(EditorMode mode, int key, const Emit& emit_literal);
    bool pending() const;
    void timeout(const Emit& emit_literal);
    void reset();

private:
    Result feed_empty(EditorMode mode, int key);

    EditorMode mode_ = NORMAL;
    bool have_mode_ = false;
    KeySeq keys_;
    std::string action_;
    std::string arg_;
    bool want_char_ = false;
};
