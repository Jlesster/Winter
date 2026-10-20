#include <stdio.h>

void test();

int main() {
    test();
    return 0;
}

void test() {
    for(int i = 0; i < 12; ++i) {
        printf("Hello World!\n");
    }
}
