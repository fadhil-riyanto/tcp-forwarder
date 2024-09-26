
#include <string.h>
#include <getopt.h>
#include "submodule/log.c-patched/src/log.h"
#include <stdint.h>
#include <stdlib.h>

#define dbgchr(x) log_info("%c", x)

#define MAX_CLIENTS 10; 

enum tcpf_mode { 
        TCPF_SERVER,
        TCPF_CLIENT
};

struct poll_queue {
        pthread_mutex_t lock;
        uint32_t        ids;
};

struct runtime_opts {
        // enum tcpf_mode mode;
        // uint16_t srcport;
        uint16_t listenport;
        
};

static void review_config(struct runtime_opts *r_opts)
{
        // printf("dest: %u\n", r_opts->destport);
        // printf("src: %u\n", r_opts->srcport);
        // printf("%s\n", (r_opts->mode == TCPF_SERVER ? "SERVER" : "CLIENT"));
        printf("socks5 server listen at %u\n", r_opts->listenport);
}

static int main_server(struct runtime_opts *r_opts)
{
        review_config(r_opts);
        
        
}

static int parseopt(int argc, char **argv, struct runtime_opts *r_opts)
{
        int optcounter = 0;
        int c = 0;

        static struct option opt_table[] = {
                {"listen", required_argument, 0, 'l'},
                {0, 0, 0, 0}
        };

        while(1) {
                c = getopt_long(argc, argv, "l", opt_table, &optcounter);

                if (c == -1)
                        break;
                switch (c) {
                        

                        case 'l':
                        r_opts->listenport = atoi(optarg);
                        break;
                }
                

        }

        
}

int main(int argc, char **argv)
{
        short ret = 0;

        struct runtime_opts runtime_opts;
        parseopt(argc, argv, &runtime_opts);

        // if (runtime_opts.mode == TCPF_SERVER) {
        ret = main_server(&runtime_opts);
        // }

        return ret;
}