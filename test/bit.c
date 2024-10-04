#include <stdio.h>

int main(void) {
    int first = 0xA;  // 1010;
    int second = 0x3; // 0011;
    int result = (first << 4) | second;
    printf("%X", result);
    return 0;
}