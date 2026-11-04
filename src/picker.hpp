#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct PickItem {
    std::string text;
    std::string detail;
    size_t id = 0;
    std::string path;
    int line = 0, col = 0;
};

struct PickerSpec {
    std::string prompt;
    bool fuzzy = true;
    bool preview = false;
    bool paths = false;  // items are file paths: icon + dimmed directory
    int max_results = 200;
    std::function<void(std::string_view)> on_query;
    std::function<void(const PickItem&)> on_accept;
    std::function<void()> on_close;
};

void picker_open(PickerSpec spec);
void picker_close();
bool picker_active();
unsigned picker_serial();
bool picker_add(std::vector<PickItem> items);
void picker_reset();

void picker_files();
void picker_grep();
void picker_register_commands();
