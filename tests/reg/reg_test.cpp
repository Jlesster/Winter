#include "../src/reg.hpp"
#include <cstdio>
#include <string>
#include "../src/shell.hpp"

static std::string g_clip;  // the fake system clipboard
static bool g_have_clip_tool = true;
ShellResult shell_run(const std::string& cmd, const std::string* input) {
    ShellResult r;
    if(!g_have_clip_tool) {
        r.code = 1;
        return r;
    }
    if(cmd.find("wl-copy") != std::string::npos) {
        g_clip = input ? *input : "";
        return r;
    }
    if(cmd.find("wl-paste") != std::string::npos) {
        r.out = g_clip;
        r.code = g_clip.empty() ? 1 : 0;
        return r;
    }
    return r;
}
static int n = 0, bad = 0;
#define CHECK(c)                                             \
    do {                                                     \
        ++n;                                                 \
        if(!(c)) {                                           \
            ++bad;                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
        }                                                    \
    } while(0)
static std::string rd(char name) {
    auto v = reg_get(name);
    return v ? v->text : "<none>";
}

int main() {
    // yank: "" and "0, and the clipboard follows
    reg_store(RegKind::Yank, "foo", false);
    CHECK(reg_read().text == "foo" && !reg_read().linewise);
    CHECK(rd('0') == "foo" && g_clip == "foo");
    CHECK(rd('1') == "<none>" && rd('-') == "<none>");

    // small delete -> "-, not "1; "0 keeps the yank
    reg_store(RegKind::Delete, "bar", false);
    CHECK(rd('-') == "bar" && rd('1') == "<none>" && rd('0') == "foo");
    CHECK(reg_read().text == "bar" && g_clip == "bar");

    // linewise deletes shift 1->2->3
    reg_store(RegKind::Delete, "a\n", true);
    reg_store(RegKind::Delete, "b\n", true);
    reg_store(RegKind::Delete, "c\n", true);
    CHECK(rd('1') == "c\n" && rd('2') == "b\n" && rd('3') == "a\n");
    CHECK(reg_read().linewise && rd('-') == "bar");
    // a charwise delete spanning lines also goes to the numbered ring
    reg_store(RegKind::Delete, "x\ny", false);
    CHECK(rd('1') == "x\ny" && rd('2') == "c\n" && rd('4') == "a\n");
    // the ring ends at 9
    for(int i = 0; i < 12; ++i)
        reg_store(RegKind::Delete, std::to_string(i) + "\n", true);
    CHECK(rd('1') == "11\n" && rd('9') == "3\n");

    // named register: stored, unnamed follows, "0 and the CLIPBOARD are
    // untouched
    const std::string clip_before = g_clip;
    {
        RegScope s('a');
        reg_store(RegKind::Yank, "named", false);
    }
    CHECK(rd('a') == "named" && rd('0') == "foo" && g_clip == clip_before);
    CHECK(reg_read().text == "named");  // p still pastes it, not the clipboard
    CHECK(rd('A') == "named");          // reading A is reading a
    // a delete into a named register does not shift the numbered ring
    {
        RegScope s('b');
        reg_store(RegKind::Delete, "gone\n", true);
    }
    CHECK(rd('b') == "gone\n" && rd('1') == "11\n");

    // uppercase appends
    {
        RegScope s('A');
        reg_store(RegKind::Yank, "+more", false);
    }
    CHECK(rd('a') == "named+more");
    {
        RegScope s('A');
        reg_store(RegKind::Yank, "line\n", true);
    }
    CHECK(rd('a') == "named+more\nline\n" && reg_get('a')->linewise);
    {
        RegScope s('A');
        reg_store(RegKind::Yank, "tail", false);
    }
    CHECK(rd('a') == "named+more\nline\ntail\n");
    {
        RegScope s('z');
        reg_store(RegKind::Yank, "x", false);
    }
    {
        RegScope s('Z');
        reg_store(RegKind::Yank, "y", false);
    }
    CHECK(rd('z') == "xy");
    {
        RegScope s('q');
        reg_store(RegKind::Yank, "fresh", false);
    }  // A on an empty register
    {
        RegScope s('Q');
        reg_store(RegKind::Yank, "!", false);
    }
    CHECK(rd('q') == "fresh!");

    // black hole: nothing changes
    reg_store(RegKind::Yank, "keep", false);
    {
        RegScope s('_');
        reg_store(RegKind::Delete, "lost\n", true);
    }
    CHECK(reg_read().text == "keep" && rd('1') == "11\n" &&
          rd('_') == "<none>");

    // external clipboard change wins for the unnamed register
    g_clip = "from outside";
    CHECK(reg_read().text == "from outside" && !reg_read().linewise);
    g_clip = "outside line\n";
    CHECK(reg_read().linewise);
    // ...but not once we copied it ourselves
    reg_store(RegKind::Yank, "mine", false);
    CHECK(reg_read().text == "mine");

    // clipboard registers
    g_clip = "clip text";
    CHECK(reg_read('+').text == "clip text" &&
          reg_read('*').text == "clip text");
    {
        RegScope s('+');
        reg_store(RegKind::Yank, "to clip", false);
    }
    CHECK(g_clip == "to clip" && reg_read().text == "to clip");
    {
        RegScope s('"');
        reg_store(RegKind::Yank, "explicit", false);
    }
    CHECK(rd('0') == "explicit");  // `""yy` is the same as `yy`

    // selection scope nests and restores
    CHECK(reg_selected() == 0);
    {
        RegScope o('a');
        CHECK(reg_selected() == 'a');
        {
            RegScope i('b');
            CHECK(reg_selected() == 'b');
        }
        CHECK(reg_selected() == 'a');
    }
    CHECK(reg_selected() == 0);
    {
        RegScope s('a');
        CHECK(reg_read().text == rd('a'));
    }  // reads honour the selection

    // scripting side
    CHECK(reg_put('m', "scripted\n", true) && rd('m') == "scripted\n" &&
          reg_get('m')->linewise);
    CHECK(reg_put('m', "s2", false) && reg_get('m')->text == "s2" &&
          !reg_get('m')->linewise);
    CHECK(reg_put('M', "+s3", false) && rd('m') == "s2+s3");
    CHECK(reg_put('5', "five", false) && rd('5') == "five");
    CHECK(reg_put('-', "dash", false) && rd('-') == "dash");
    CHECK(reg_put('_', "x", false) && rd('_') == "<none>");
    CHECK(!reg_put('?', "x", false) && !reg_put('%', "x", false));
    reg_provider('%', [] { return std::string("file.txt"); });
    CHECK(rd('%') == "file.txt" && reg_name_ok('%') &&
          !reg_put('%', "x", false));
    {
        RegScope s('%');
        reg_store(RegKind::Yank, "never", false);
    }  // read-only: ignored
    CHECK(reg_read().text == "explicit");
    CHECK(rd('?') == "<none>" && !reg_name_ok('?') && !reg_name_ok('/') &&
          !reg_name_ok(' '));
    reg_put('"', "via put", false);
    CHECK(reg_read().text == "via put" && g_clip == "via put");
    CHECK(reg_put('c', "c-only", false) &&
          reg_read().text == "via put");  // put does not touch ""
    const auto names = reg_list();
    std::string s(names.begin(), names.end());
    CHECK(s.find('"') == 0 && s.find('0') != std::string::npos &&
          s.find('a') != std::string::npos && s.find('%') != std::string::npos);
    CHECK(s.find('_') == std::string::npos);

    // the . register: text typed in the last insert session
    CHECK(reg_last_insert().empty());
    reg_insert_begin(); reg_insert_typed("hello"); reg_insert_typed(" world"); reg_insert_end();
    CHECK(reg_last_insert() == "hello world");
    reg_insert_begin(); reg_insert_end();                        // typed nothing: the old text stays
    CHECK(reg_last_insert() == "hello world");
    reg_insert_begin(); reg_insert_typed("abc"); reg_insert_erase(RegErase::Char); reg_insert_end();
    CHECK(reg_last_insert() == "ab");                            // backspace
    reg_insert_begin(); reg_insert_typed("caf\xC3\xA9"); reg_insert_erase(RegErase::Char); reg_insert_end();
    CHECK(reg_last_insert() == "caf");                           // one whole UTF-8 character, not one byte
    reg_insert_begin(); reg_insert_typed("foo bar"); reg_insert_erase(RegErase::Word); reg_insert_end();
    CHECK(reg_last_insert() == "foo ");                          // CTRL-W removes the last word
    reg_insert_begin(); reg_insert_typed("foo bar  "); reg_insert_erase(RegErase::Word); reg_insert_end();
    CHECK(reg_last_insert() == "foo ");                          // ...skipping trailing blanks first
    reg_insert_begin(); reg_insert_typed("x\ny"); reg_insert_erase(RegErase::Word); reg_insert_end();
    CHECK(reg_last_insert() == "x\n");                           // a newline separates words too
    reg_insert_begin(); reg_insert_typed("gone"); reg_insert_erase(RegErase::All); reg_insert_typed("kept"); reg_insert_end();
    CHECK(reg_last_insert() == "kept");                          // CTRL-U
    reg_insert_begin(); reg_insert_typed("stale"); reg_insert_begin(); reg_insert_typed("fresh"); reg_insert_end();
    CHECK(reg_last_insert() == "fresh");                         // a new session starts clean
    reg_insert_begin(); reg_insert_erase(RegErase::Char); reg_insert_erase(RegErase::Word); reg_insert_end();   // erasing nothing is fine
    CHECK(reg_last_insert() == "fresh");
    // as a register: readable, never writable
    reg_provider('.', [] { return reg_last_insert(); });
    CHECK(reg_name_ok('.') && rd('.') == "fresh");
    CHECK(!reg_put('.', "x", false) && rd('.') == "fresh");
    { RegScope s('.'); reg_store(RegKind::Yank, "ignored", false); }
    CHECK(rd('.') == "fresh");
    reg_provider(':', [] { return std::string("w | q"); }); reg_provider('/', [] { return std::string("needle"); });
    CHECK(rd(':') == "w | q" && rd('/') == "needle" && !reg_put(':', "x", false) && !reg_put('/', "x", false));
    { RegScope s('/'); CHECK(reg_read().text == "needle"); }      // `"/p` pastes the pattern
    { const auto l = reg_list(); const std::string ls(l.begin(), l.end()); CHECK(ls.find('.') != std::string::npos && ls.find(':') != std::string::npos && ls.find('/') != std::string::npos); }

    // no clipboard tool: everything still works inside the editor
    g_have_clip_tool = false;
    reg_store(RegKind::Yank, "internal", false);
    CHECK(reg_read().text == "internal");
    {
        RegScope s('k');
        reg_store(RegKind::Yank, "kk", false);
    }
    CHECK(reg_read().text == "kk");

    std::printf("%d checks, %d failed\n", n, bad);
    return bad != 0;
}
