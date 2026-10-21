#include <cstdio>
#include <string>

#include "../src/options.hpp"

static int failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if(!(cond)) {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failed;                                                    \
        }                                                                \
    } while(0)

int main() {
    // defaults match the old #defines
    CHECK(opts.tab_stop == 4 && opts.shift_width == 4 && opts.expand_tab);
    CHECK(opts.quit_times == 3);

    // ints: set, alias, range, junk
    CHECK(set_option("tabstop", "8") == OptResult::Ok && opts.tab_stop == 8);
    CHECK(set_option("sw", "2") == OptResult::Ok && opts.shift_width == 2);
    CHECK(set_option("ts", "0") == OptResult::BadValue && opts.tab_stop == 8);
    CHECK(set_option("ts", "17") == OptResult::BadValue);
    CHECK(set_option("ts", "4x") == OptResult::BadValue);
    CHECK(set_option("ts", "") == OptResult::BadValue);
    CHECK(set_option("ts", "-1") == OptResult::BadValue);
    CHECK(set_option("nonsense", "1") == OptResult::Unknown);
    CHECK(set_option("cursor_insert", "6") == OptResult::Ok);
    CHECK(set_option("cursor_insert", "7") == OptResult::BadValue);

    // bools
    for(const char* v : {"false", "0", "off", "no"}) {
        opts.expand_tab = true;
        CHECK(set_option("expandtab", v) == OptResult::Ok && !opts.expand_tab);
    }
    for(const char* v : {"true", "1", "on", "yes"}) {
        opts.expand_tab = false;
        CHECK(set_option("et", v) == OptResult::Ok && opts.expand_tab);
    }
    CHECK(set_option("et", "maybe") == OptResult::BadValue);

    // get
    CHECK(get_option("tabstop") == "8");
    CHECK(get_option("et") == "true");
    CHECK(!get_option("nonsense"));
    CHECK(option_names().size() >= 7);

    // :set parsing
    opts = Options{};
    CHECK(apply_set_command("ts=2 sw=3") == "");
    CHECK(opts.tab_stop == 2 && opts.shift_width == 3);
    CHECK(apply_set_command("noexpandtab") == "" && !opts.expand_tab);
    CHECK(apply_set_command("noet") == "" && !opts.expand_tab);
    CHECK(apply_set_command("expandtab") == "" && opts.expand_tab);
    CHECK(apply_set_command("ts?") == "ts=2");
    CHECK(apply_set_command("tabstop") == "tabstop=2");           // int, no value: show
    CHECK(apply_set_command("ts=2 sw?") == "sw=3");
    CHECK(apply_set_command("bogus") == "Unknown option: bogus");
    CHECK(apply_set_command("bogus=1") == "Unknown option: bogus");
    CHECK(apply_set_command("ts=99") == "Invalid value for ts: 99");
    CHECK(apply_set_command("ts=1 bogus ts?") == "Unknown option: bogus, ts=1");
    CHECK(apply_set_command("") == "");
    CHECK(apply_set_command("no") == "Unknown option: no");

    std::printf(failed ? "%d FAILED\n" : "options: all passed\n", failed);
    return failed != 0;
}
