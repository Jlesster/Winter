#include "../src/editor.hpp"
#include "../src/fold_buf.hpp"
#include "../src/indent_model.hpp"
#include "../src/multicursor.hpp"
#include "../src/syntax.hpp"

// Minimal stand-ins for the editor state the unit tests don't exercise. These
// are deliberately no-ops: option/register tests care about their own state.
EditorConfig E;

FoldConfig& fold_config() {
    static FoldConfig cfg;
    return cfg;
}

namespace indent {
void Model::on_edit(const TextEdit&) {}
}  // namespace indent

Syntax::~Syntax() {}

bool mc_running() { return false; }
uint64_t mc_pass() { return 0; }
size_t mc_count() { return 0; }
size_t mc_index() { return 0; }
