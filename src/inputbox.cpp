#include "inputbox.hpp"
#include <cstddef>
#include <memory>
#include <string_view>
#include "core.hpp"
#include "keys.hpp"
#include "overlay.hpp"
#include "columns.hpp"
#include "theme.hpp"
#include "utf8.hpp"

namespace {
struct Input {
    InputSpec spec;
    OverlayId id = 0;
    std::string text;
    size_t cur = 0;
    int scroll = 0;
    std::string pend;
    bool done = false;

    void changed() {
        if(spec.on_change) spec.on_change(text);
    }
    void insert(std::string_view s) {
        text.insert(cur, s);
        cur += s.size();
        changed();
    }
    void erase(size_t a, size_t b) {
        text.erase(a, b - a);
        cur = a;
        changed();
    }

    size_t word_start() const {
        const auto sep = [&](size_t i) {
            return text[i] == ' ' || text[i] == '/';
        };
        size_t i = cur;
        while(i > 0 && sep(i - 1)) --i;
        while(i > 0 && !sep(i - 1)) --i;
        return i;
    }

    void feed_byte(int c) {
        const char b = static_cast<char>(c);
        if(!utf8_is_cont(b)) pend.clear();
        else if(pend.empty()) return;
        pend += b;
        if(pend.size() == utf8_seq_len(pend[0])) {
            insert(pend);
            pend.clear();
        }
    }

    void submit() {
        done = true;
        auto cb = std::move(spec.on_submit);
        std::string t = text;
        overlay_close(id);
        if(cb) cb(std::move(t));
    }

    void key(int c) {
        if(c >= 0x80 && c < 0x100) {
            feed_byte(c);
            return;
        }
        pend.clear();
        if(c >= 32 && c < 127) {
            insert(std::string(1, static_cast<char>(c)));
            return;
        }
        switch(c) {
            case '\r': submit(); break;
            case CTRL_KEY('c'): overlay_close(id); break;
            case ARROW_LEFT: cur = utf8_prev(text, cur); break;
            case ARROW_RIGHT: cur = utf8_next(text, cur); break;
            case HOME:
            case CTRL_KEY('a'): cur = 0; break;
            case END:
            case CTRL_KEY('e'): cur = text.size(); break;
            case BACKSPACE:
                if(cur) erase(utf8_prev(text, cur), cur);
                break;
            case DELETE:
                if(cur < text.size()) {
                    const size_t n = utf8_next(text, cur);
                    text.erase(cur, n - cur);
                    changed();
                }
                break;
            case CTRL_BACKSPACE:
            case CTRL_KEY('h'):
            case CTRL_KEY('w'): erase(word_start(), cur); break;
            case CTRL_KEY('u'): erase(0, cur); break;
            default: break;
        }
    }

    static std::string_view window(std::string_view s, int scroll, int avail,
                                   int& lead) {
        size_t i = 0;
        int col = 0;
        while(i < s.size() && col < scroll) {
            const Decoded d = utf8_decode(s, i);
            col += cell_width(d.cp, 0);
            i += d.len;
        }
        lead = col - scroll;
        size_t j = i;
        int used = lead;
        while(j < s.size()) {
            const Decoded d = utf8_decode(s, j);
            const int w = cell_width(d.cp, 0);
            if(used + w > avail) break;
            used += w;
            j += d.len;
        }
        return s.substr(i, j - i);
    }

    void paint() {
        const int pw = text_width(spec.prompt);
        const int avail = std::max(1, spec.w - 2 - pw);
        const int ccol = text_width(std::string_view(text).substr(0, cur));
        if(ccol < scroll) scroll = ccol;
        if(ccol >= scroll + avail) scroll = ccol - avail + 1;
        int lead = 0;
        const std::string_view vis = window(text, scroll, avail, lead);
        const Style base =
            spec.style == Style{} ? theme_ui("ui.input", Style{}) : spec.style;
        const Style kPromptDflt{.attrs = ATTR_BOLD};
        Style prompt = spec.prompt_style == kPromptDflt
                           ? theme_ui("ui.input.prompt", kPromptDflt)
                           : spec.prompt_style;
        prompt.bg = base.bg;
        overlay_clear(id, base);
        overlay_put(id, 1, 1, spec.prompt, prompt);
        overlay_put(id, 1 + pw + lead, 1, vis, base);
        overlay_set_cursor(id, 1 + pw + (ccol - scroll), 1);
    }
};
}  // namespace

OverlayId input_open(InputSpec in) {
    auto st = std::make_shared<Input>();
    st->text = in.initial;
    st->cur = st->text.size();
    st->spec = std::move(in);

    OverlaySpec o;
    o.at = st->spec.at;
    o.x = st->spec.x;
    o.y = st->spec.y;
    o.w = std::max(st->spec.w, text_width(st->spec.prompt) + 6);
    st->spec.w = o.w;
    o.h = 3;
    o.z = 100;
    o.modal = true;
    o.pass.clear();
    o.framed = true;
    o.border = st->spec.border;
    o.border_style = st->spec.border_style != Style{} ? st->spec.border_style
                     : st->spec.style == Style{}
                         ? theme_ui("ui.input.border", Style{})
                         : st->spec.style;
    o.title = st->spec.title;
    o.tee_bottom = st->spec.join_below;
    o.on_key = [st](int c) {
        if(st->spec.on_key && st->spec.on_key(c)) return true;
        if(c == '\x1b') return false;
        st->key(c);
        return true;
    };
    o.on_paint = [st](OverlayId) { st->paint(); };
    o.on_close = [st] {
        if(st->done) return;
        st->done = true;
        if(st->spec.on_cancel) st->spec.on_cancel();
    };
    st->id = overlay_open(std::move(o));
    return st->id;
}
