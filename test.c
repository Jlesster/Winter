#include <stdio.h>

void swap(int* a, int* b) {
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}

int main() {
    int a = 5;
    int b = 12;
    swap(&a, &b);
    printf("A:%i, B: %i\n", a, b);
    return 0;
}
