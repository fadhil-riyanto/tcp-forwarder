#include <stdio.h>

int main()
{
        char data[] = "abcdefg";

        *data = *data + 1;

        printf("%s\n", data);
}