#pragma once
#include <cstddef>
#include <string_view>
#include <vector>

struct LineSpan;

void snippet_init();
bool snippet_expand(std::string_view body);
bool snippet_active();
void snippet_next();
bool snippet_pre_key(int c);
void snippet_post_key();
void snippet_end();
void snippet_add(std::string trigger, std::string body);
bool snippet_try_trigger();
void snippet_spans(size_t ls, size_t len, std::vector<LineSpan>& out);
