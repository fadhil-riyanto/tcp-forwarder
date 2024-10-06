#include <stdio.h>

struct aaa {
        int a;
        void* b;
} __attribute__((__aligned__(8)));

int main()
{
        printf("%lu\n", sizeof(struct aaa) - sizeof(int));
}