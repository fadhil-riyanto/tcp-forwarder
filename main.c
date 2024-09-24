
#include <stdio.h>
#include <getopt.h>
#include "submodule/log.c-patched/src/log.h"

#define dbgchr(x) log_info("%c", x)

static int parseopt(int argc, char **argv)
{
        int optcounter = 0;
        int c = 0;

        static struct option opt_table[] = {
                {"mode", required_argument, 0, 'm'},
                {0, 0, 0, 0}
        };

        while(1) {
                c = getopt_long(argc, argv, "m:", opt_table, &optcounter);
                dbgchr(c);

                if (c == -1)
                        break;

        }

        
}

int main(int argc, char **argv)
{
        parseopt(argc, argv);
}