#include <string.h>
#include <getopt.h>
#include "submodule/log.c-patched/src/log.h"

#define dbgchr(x) log_info("%c", x)

enum tcpf_mode {
        TCPF_SERVER,
        TCPF_CLIENT
};

struct runtime_opts {
        enum tcpf_mode mode;
};

static int parseopt(int argc, char **argv, struct runtime_opts *r_opts)
{
        int optcounter = 0;
        int c = 0;

        static struct option opt_table[] = {
                {"mode", required_argument, 0, 'm'},
                // {"mode", required_argument, 0, 'm'},4
                {0, 0, 0, 0}
        };

        while(1) {
                c = getopt_long(argc, argv, "m:", opt_table, &optcounter);
                // dbgchr(c);

                if (c == -1)
                        break;

                switch (c) {
                        case 'm':
                        if (strcmp(optarg, "server") == 0) {
                                r_opts->mode = TCPF_SERVER;
                        }

                        if (strcmp(optarg, "client") == 0) {
                                r_opts->mode = TCPF_CLIENT;
                        }
                        break;
                }
                

        }

        
}

int main(int argc, char **argv)
{

        struct runtime_opts runtime_opts;
        parseopt(argc, argv, &runtime_opts);

        return 0;
}