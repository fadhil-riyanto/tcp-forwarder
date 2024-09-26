
#include <asm-generic/socket.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include "submodule/log.c-patched/src/log.h"
#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

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
        char*    addr;
        
};

struct server_ctx {
        int tcpfd;
};

static void review_config(struct runtime_opts *r_opts)
{
        // printf("dest: %u\n", r_opts->destport);
        // printf("src: %u\n", r_opts->srcport);
        // printf("%s\n", (r_opts->mode == TCPF_SERVER ? "SERVER" : "CLIENT"));
        printf("socks5 server listen at %u\n", r_opts->listenport);
}

static void r_opts_clean(struct runtime_opts *r_opts)
{
        free(r_opts->addr);
}

static int setup_addr_storage(struct sockaddr_storage *ss_addr, struct runtime_opts *r_opts)
{
        int ret = 0;
        struct sockaddr_in *sockaddr_v4 = (struct sockaddr_in*)ss_addr;

        memset(sockaddr_v4, 0, sizeof(*sockaddr_v4));

        ret = inet_pton(AF_INET, r_opts->addr, &sockaddr_v4->sin_addr);

        if (ret == 1) {
                sockaddr_v4->sin_family = AF_INET;
                sockaddr_v4->sin_port = htons(r_opts->listenport);
                return 0;
        }

        return -1;
}

static int create_sock_ret_fd(struct sockaddr_storage *ss_addr)
{
        socklen_t len = sizeof(struct sockaddr_in);
        int ret = 0;
        int fd;

        fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        
        if (fd < 0) {
                perror("socket()");
                return -1;
        }

        int value = 1;

        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value,  sizeof(len));

        ret = bind(fd, (struct sockaddr*)ss_addr, len);
        if (ret < 0) {
                perror("bind()");
                close(fd);
                return -1;
        }

        ret = listen(fd, 256);
        if (ret < 0) {
                perror("listen()");
                close(fd);
                return -1;
        }

        return fd;
}

static int main_server(struct runtime_opts *r_opts)
{
        int ret = 0;
        struct server_ctx *srv_ctx;
        struct sockaddr_storage ss_addr;

        review_config(r_opts);

        if (setup_addr_storage(&ss_addr, r_opts) == -1) {
                fprintf(stderr, "Invalid ip format\n");
        }

        if ((ret = create_sock_ret_fd(&ss_addr)) == -1) {
                fprintf(stderr, "socket failed\n");
        }

        srv_ctx->tcpfd = ret;

        printf("server listening on %s:%d\n", r_opts->addr, r_opts->listenport);
        

        r_opts_clean(r_opts);
        close(ret);

        return 0;
        
}

static int parseopt(int argc, char **argv, struct runtime_opts *r_opts)
{
        int optcounter = 0;
        int c = 0;

        static struct option opt_table[] = {
                {"listen", required_argument, 0, 'l'},
                {"addr", required_argument, 0, 'a'},
                {0, 0, 0, 0}
        };

        while(1) {
                c = getopt_long(argc, argv, "l:a:", opt_table, &optcounter);

                if (c == -1)
                        break;
                switch (c) {
                
                        case 'l':
                        r_opts->listenport = atoi(optarg);
                        break;

                        case 'a':
                        // strcpy(, optarg);
                        r_opts->addr = strdup(optarg);
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