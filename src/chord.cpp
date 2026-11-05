#include "chord.hpp"

#include <utility>

#include "utf8.hpp"

namespace {
KeySeq one(int key) { return KeySeq{static_cast<char32_t>(key)}; }

void run_action(const std::string& action, std::string arg) {
    Ctx ctx{0, std::move(arg)};
    registry().run_action(action, ctx);
}
}  // namespace

void Chord::reset() {
    keys_.clear();
    action_.clear();
    arg_.clear();
    want_char_ = false;
}

bool Chord::pending() const { return !keys_.empty() && !want_char_; }

Chord::Result Chord::feed(EditorMode mode, int key, const Emit& emit) {
    if(have_mode_ && mode != mode_) reset();
    mode_ = mode;
    have_mode_ = true;

    // Esc cancels a pending chord and then acts normally.
    if(key == '\x1b' && !keys_.empty()) {
        reset();
        return Result::Pass;
    }

    // The next key after a wants_char action is its argument.
    if(want_char_) {
        if(key == '\x1b' || key >= 0x100) {
            reset();
            return Result::Handled;
        }
        if(arg_.empty() && key < 32) {
            reset();
            return Result::Handled;
        }
        arg_ += static_cast<char>(key);
        if(arg_.size() < utf8_seq_len(arg_[0])) return Result::Handled;

        std::string action = std::move(action_);
        std::string arg = std::move(arg_);
        reset();
        run_action(action, std::move(arg));
        return Result::Handled;
    }

    if(keys_.empty()) return feed_empty(mode, key);

    KeySeq probe = keys_;
    probe += static_cast<char32_t>(key);
    const Hit hit = registry().lookup(mode, probe);

    if(hit.kind == Lookup::None) {
        // This key no longer continues the chord. Type the pending keys,
        // then process the current key from a fresh state.
        const KeySeq pending = std::move(keys_);
        reset();
        for(const char32_t k : pending) emit(static_cast<int>(k));
        return feed_empty(mode, key);
    }

    if(hit.kind == Lookup::Prefix || hit.longer) {
        keys_ = std::move(probe);
        return Result::Buffered;
    }

    keys_.clear();
    action_ = hit.action;
    if(registry().wants_char(action_)) {
        want_char_ = true;
        arg_.clear();
        return Result::Handled;
    }

    const std::string action = std::move(action_);
    reset();
    run_action(action, {});
    return Result::Handled;
}

Chord::Result Chord::feed_empty(EditorMode mode, int key) {
    const KeySeq probe = one(key);
    const Hit hit = registry().lookup(mode, probe);

    if(hit.kind == Lookup::None) return Result::Pass;

    if(hit.kind == Lookup::Prefix || hit.longer) {
        keys_ = probe;
        return Result::Buffered;
    }

    action_ = hit.action;
    if(registry().wants_char(action_)) {
        want_char_ = true;
        arg_.clear();
        return Result::Handled;
    }

    const std::string action = std::move(action_);
    reset();
    run_action(action, {});
    return Result::Handled;
}

void Chord::timeout(const Emit& emit) {
    if(want_char_ || keys_.empty()) return;

    const Hit hit = registry().lookup(mode_, keys_);
    if(hit.kind == Lookup::Exact) {
        action_ = hit.action;
        keys_.clear();
        if(registry().wants_char(action_)) {
            want_char_ = true;
            arg_.clear();
            return;
        }

        const std::string action = std::move(action_);
        reset();
        run_action(action, {});
        return;
    }

    // Prefix only: nothing completed the chord, so type what was pressed.
    const KeySeq pending = std::move(keys_);
    reset();
    for(const char32_t k : pending) emit(static_cast<int>(k));
}
