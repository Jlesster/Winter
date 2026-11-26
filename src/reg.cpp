/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "reg.hpp"

#include <cctype>
#include <map>
#include <utility>

#include "multicursor.hpp"
#include "shell.hpp"

namespace {

std::map<char, RegValue> g_regs;  // 0 1-9 - a-z
RegValue g_unnamed;               // what "" points at
std::string g_clip_seen;          // the clipboard text as we last wrote/read it
char g_selected = 0;
std::map<char, std::function<std::string()>> g_providers;

void clip_set(const std::string& s) {
    shell_run(
        "if command -v wl-copy >/dev/null 2>&1; then wl-copy >/dev/null 2>&1;"
        " elif command -v xclip >/dev/null 2>&1;"
        " then xclip -selection clipboard >/dev/null 2>&1; fi",
        &s);
}

std::optional<std::string> clip_get() {
    const ShellResult r = shell_run(
        "if command -v wl-paste >/dev/null 2>&1; then wl-paste --no-newline "
        "2>/dev/null;"
        " elif command -v xclip >/dev/null 2>&1;"
        " then xclip -o -selection clipboard 2>/dev/null; else exit 1; fi",
        nullptr);
    if(r.code != 0 || r.out.empty()) return std::nullopt;
    return r.out;
}

RegValue from_clip(const std::string& s) { return {s, s.back() == '\n'}; }

bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// the text goes after dst; mixing charwise and linewise makes it linewise
void append_to(RegValue& dst, const RegValue& add) {
    if(dst.text.empty()) {
        dst = add;
        return;
    }
    if(!dst.linewise && !add.linewise) {
        dst.text += add.text;
        return;
    }
    std::string t = dst.text;
    if(!dst.linewise) t += '\n';
    t += add.text;
    if(!add.linewise) t += '\n';
    dst = {std::move(t), true};
}

// Per-cursor texts, for the registers that mc_each fills one cursor at a time.
// `cur` is what the latest pass that wrote the register put there; once a new
// pass starts writing, what was in `cur` moves to `prev`, which is what that
// pass reads (visual p writes the replaced text while the cursors after it
// still have to read what they are about to paste).
struct McSlots {
    uint64_t pass = 0;
    std::vector<std::optional<RegValue>> cur, prev;
};
std::map<char, McSlots> g_mc;

char lower_of(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// A register was written by `sel` (0: the unnamed one). The unnamed register
// follows every write, so its slots go with it.
void mc_note_write(char sel, const RegValue& v) {
    const char key = sel == 0 ? '"' : lower_of(sel);
    const bool per_cursor = mc_running() && !is_upper(sel) &&
                            (key == '"' || is_lower(key));
    for(const char k : {key, '"'}) {
        if(!per_cursor) {
            g_mc.erase(k);
            continue;
        }
        McSlots& s = g_mc[k];
        if(s.pass != mc_pass()) {
            s.pass = mc_pass();
            s.prev = std::move(s.cur);
            s.cur.assign(mc_count(), std::nullopt);
        }
        if(mc_index() < s.cur.size()) s.cur[mc_index()] = v;
    }
}

// the unnamed register and the clipboard follow each other
void set_unnamed_and_clip(const RegValue& v) {
    g_unnamed = v;
    // Under mc_each every cursor writes in turn: the last one puts its text on
    // the clipboard, not one shell per cursor.
    if(mc_running() && mc_index() + 1 < mc_count()) return;
    clip_set(v.text);
    g_clip_seen = v.text;
}

}  // namespace

bool reg_name_ok(char c) {
    return c == '"' || c == '_' || c == '-' || c == '+' || c == '*' ||
           is_digit(c) || is_lower(c) || is_upper(c) || g_providers.contains(c);
}

RegScope::RegScope(char name) : prev_(g_selected) { g_selected = name; }
RegScope::~RegScope() { g_selected = prev_; }
char reg_selected() { return g_selected; }

void reg_store(RegKind kind, std::string text, bool linewise) {
    const char sel = g_selected;
    if(sel == '_' || g_providers.contains(sel)) return;  // nowhere to put it
    RegValue v{std::move(text), linewise};
    mc_note_write(sel, v);

    if(sel == 0 || sel == '"') {
        if(kind == RegKind::Yank) {
            g_regs['0'] = v;
        } else if(!v.linewise && v.text.find('\n') == std::string::npos) {
            g_regs['-'] = v;  // within one line
        } else {
            for(char c = '9'; c > '1'; --c)
                if(const auto it = g_regs.find(c - 1); it != g_regs.end())
                    g_regs[c] = it->second;
                else g_regs.erase(c);
            g_regs['1'] = v;
        }
        set_unnamed_and_clip(v);
        return;
    }
    if(sel == '+' || sel == '*') {
        set_unnamed_and_clip(v);
        return;
    }
    // named (and explicit 0-9, -): the clipboard stays as it was
    const char key = is_upper(sel) ? static_cast<char>(sel - 'A' + 'a') : sel;
    if(is_upper(sel)) {
        RegValue cur = g_regs.contains(key) ? g_regs[key] : RegValue{};
        append_to(cur, v);
        g_regs[key] = std::move(cur);
    } else {
        g_regs[key] = v;
    }
    g_unnamed = g_regs[key];
}

std::optional<RegValue> reg_mc_slot(char name) {
    if(!mc_running()) return std::nullopt;
    if(name == 0) name = g_selected;
    const auto it = g_mc.find(name == 0 ? '"' : name);
    if(it == g_mc.end()) return std::nullopt;
    const McSlots& s = it->second;
    const auto& v = s.pass == mc_pass() ? s.prev : s.cur;  // see McSlots
    if(v.size() != mc_count() || mc_index() >= v.size()) return std::nullopt;
    return v[mc_index()];
}

void reg_mc_forget() { g_mc.clear(); }

RegValue reg_read(char name) {
    if(name == 0) name = g_selected;
    if(const auto s = reg_mc_slot(name)) return *s;
    if(name == 0 || name == '"') {
        if(const auto c = clip_get(); c && *c != g_clip_seen) {
            g_unnamed = from_clip(*c);  // copied elsewhere since we last looked
            g_clip_seen = *c;
        }
        return g_unnamed;
    }
    if(name == '_') return {};
    if(name == '+' || name == '*') {
        const auto c = clip_get();
        return c ? from_clip(*c) : RegValue{};
    }
    if(const auto it = g_providers.find(name); it != g_providers.end())
        return {it->second(), false};
    if(is_upper(name)) name = static_cast<char>(name - 'A' + 'a');
    const auto it = g_regs.find(name);
    return it == g_regs.end() ? RegValue{} : it->second;
}

std::optional<RegValue> reg_get(char name) {
    if(!reg_name_ok(name) || name == '_') return std::nullopt;
    RegValue v = reg_read(name);
    if(v.text.empty()) return std::nullopt;
    return v;
}

bool reg_put(char name, std::string text, bool linewise) {
    RegValue v{std::move(text), linewise};
    if(name == '_') return true;
    g_mc.erase(lower_of(name));
    if(name == '"' || name == '+' || name == '*') g_mc.erase('"');
    if(name == '"' || name == '+' || name == '*') {
        set_unnamed_and_clip(v);
        return true;
    }
    if(g_providers.contains(name)) return false;  // read only
    if(!(is_lower(name) || is_upper(name) || is_digit(name) || name == '-'))
        return false;
    if(is_upper(name)) {
        const char key = static_cast<char>(name - 'A' + 'a');
        RegValue cur = g_regs.contains(key) ? g_regs[key] : RegValue{};
        append_to(cur, v);
        g_regs[key] = std::move(cur);
    } else {
        g_regs[name] = std::move(v);
    }
    return true;
}

std::vector<char> reg_list() {
    std::vector<char> out;
    if(!g_unnamed.text.empty()) out.push_back('"');
    for(const auto& [k, v] : g_regs)  // std::map: '-' 0-9 a-z already in order
        if(!v.text.empty() && k != '-') out.push_back(k);
    if(const auto it = g_regs.find('-'); it != g_regs.end() && !it->second.text.empty())
        out.push_back('-');
    for(const auto& [k, fn] : g_providers)
        if(!fn().empty()) out.push_back(k);
    return out;
}

void reg_provider(char name, std::function<std::string()> fn) {
    g_providers[name] = std::move(fn);
}

// ---- the . register ----------------------------------------------------------------
namespace {
std::string g_session;      // typed so far in the insert session that is running
std::string g_last_insert;  // the last session that typed anything
}  // namespace

void reg_insert_begin() { g_session.clear(); }

void reg_insert_typed(std::string_view text) { g_session += text; }

void reg_insert_erase(RegErase what) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n'; };
    switch(what) {
        case RegErase::All: g_session.clear(); break;
        case RegErase::Char:
            // one UTF-8 character: drop continuation bytes, then the lead byte
            while(!g_session.empty() &&
                  (static_cast<unsigned char>(g_session.back()) & 0xC0) == 0x80)
                g_session.pop_back();
            if(!g_session.empty()) g_session.pop_back();
            break;
        case RegErase::Word:
            while(!g_session.empty() && space(g_session.back())) g_session.pop_back();
            while(!g_session.empty() && !space(g_session.back())) g_session.pop_back();
            break;
    }
}

void reg_insert_end() {
    if(!g_session.empty()) g_last_insert = std::move(g_session);
    g_session.clear();
}

std::string reg_last_insert() { return g_last_insert; }
