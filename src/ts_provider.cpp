// The editor's side of winter.ts: buffers and their Syntax objects.
#include <algorithm>
#include <string>

#include "editor.hpp"
#include "filetype.hpp"
#include "script_internal.hpp"
#include "syntax.hpp"
#include "tsapi.hpp"

void ts_install_provider() {
    TsProvider p;
    p.snapshot = [](uint64_t id, TsSnapshot& out) {
        Buffer* b = nullptr;
        if(id == 0) {
            b = &E.buf();
        } else {
            for(const auto& x : E.buffers)
                if(x->id == id) b = x.get();
        }
        if(!b) return false;
        // reparse if the text moved on: the same thing the renderer does
        b->syntax.update(b->pt, b->eol);
        out.buf = b->id;
        out.tree = b->syntax.tree();
        out.gen = b->syntax.tree_gen();
        out.lang = std::string(b->syntax.lang());
        const PieceTable* pt = &b->pt;
        out.text = [pt](size_t a, size_t e) {
            const size_t n = pt->size();  // the tree may count a final newline
            a = std::min(a, n);
            e = std::min(e, n);
            return a < e ? pt->text(a, e - a) : std::string();
        };
        return true;
    };
    p.language = [](std::string_view name) {
        return Syntax::language_by_name(ft::normalize(std::string(name)));
    };
    ts_set_provider(std::move(p));
}
