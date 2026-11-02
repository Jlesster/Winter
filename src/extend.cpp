#include "extend.hpp"
#include "editor.hpp"
#include "normal.hpp"
#include "registry.hpp"
#include "utf8.hpp"

void ext_operator(const std::string &name, Operator fn, std::string_view keys) {
    const std::string op = "user.op." + name;
    const std::string k = parse_keys(keys);
    registry().add_operator(op, std::move(fn));
    registry().add_action(op, [op](Ctx& c) {
        normal_begin_operator(op, c.count);
    });
    registry().map(NORMAL, k, op);
    registry().map(OPPEND, k, "motion.line");

    const std::string vis = "visual." + op;
    registry().add_action(vis, [op](Ctx&) {
        const Range r = visual_range();
        set_mode(NORMAL);
        run_operator_on(op, r);
    });
    registry().map(VISUAL, k, vis);
}

void ext_motion(const std::string& name, motion::Fn fn, std::string_view keys, bool wants_char) {
    const std::string m = "user.motion." + name;
    const std::string k = parse_keys(keys);
    registry().add_motion(m, fn);
    registry().add_action(m, [fn](Ctx& c) {
        apply_motion(fn(E.buf().pt, cursor_offset(), c.count, c.arg));
    }, wants_char);
    for(const EditorMode md : {NORMAL, OPPEND, VISUAL}) registry().map(md, k, m);
}

void ext_object(const std::string &name, ObjectFn fn, std::string_view keys) {
    const std::string o = "user.obj." + name;
    const std::string k = parse_keys(keys);
    registry().add_object(o, fn);
    registry().map(OPPEND, k, o);
    const std::string vis = "visual." + o;
    registry().add_action(vis, [fn](Ctx& c) {
        const auto r = fn(E.buf().pt, cursor_offset(), c.count);
        if(!r || r->end <= r->start) return;
        size_t last = r->end - 1;
        while(last > r->start && utf8_is_cont(E.buf().pt.at(last))) --last;
        E.v_anchor = r->start;
        E.v_line = false;
        set_cursor_offset(last);
        update_want();
    });
    registry().map(VISUAL, k, vis);
}
