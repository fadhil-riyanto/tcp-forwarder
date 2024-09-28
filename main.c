
#include <asm-generic/socket.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include "submodule/log.c-patched/src/log.h"
#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

#include "config.h"

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

struct epoll_fd_queue {
        struct epoll_event *events_arrive;
        int i;
};

struct udata {
        struct sockaddr *addr;
        int reserved;
};

struct posix_thread_poll_thread {
        pthread_t pthread;
        int state;
};

struct posix_thread_handler
{
        struct posix_thread_poll_thread poll_thread;
        struct posix_thread_poll_thread poll_recv_thread;
};


struct _fd_sockaddr_list {
        struct sockaddr *sockaddr;
        int fd;
        int is_active;
};

struct fd_sockaddr_list {
        struct _fd_sockaddr_list *list;
        int size;
};

struct server_ctx {
        /* our tcp-fd */
        int tcpfd;

        /* handle tcp poll */
        int epoll_fd;

        /* monitor client accept */
        int epoll_recv_fd;
        struct epoll_event *acceptfd_watchlist_event;

        /* hold stack ptr from enter_eventloop func */
        struct fd_sockaddr_list *fd_sockaddr_list;
        
        struct epoll_fd_queue *epoll_fd_queue; /* probably unused */

        volatile int *need_exit_ptr;
};

volatile int g_need_exit = 0;

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

static void signal_cb(int signum)
{
        printf("signal detected, exiting...\n");
g_need_exit = 1;

}

static int server_reg_sigaction(void)
{       
        struct sigaction sa;
        int ret;
        
        memset(&sa, 0, sizeof(struct sigaction));

        sa.sa_handler = signal_cb;

        ret = sigaction(SIGINT, &sa, NULL);
        if (ret < 0) {
                return -1;
        }

        return 0;

}


static void setup_epoll(struct server_ctx *srv_ctx)
{
        srv_ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        srv_ctx->epoll_recv_fd = epoll_create(EPOLL_CLOEXEC);
}

// static void install_fd2epoll(int intrest_fd, struct server_ctx *srv_ctx)
// {
//         struct epoll_event *cur_stack_epoll_event = &srv_ctx->epoll_fd_queue->eventmode[srv_ctx->epoll_fd_queue->i];
//         cur_stack_epoll_event->events = EPOLLIN;

//         epoll_ctl(
//                 srv_ctx->epoll_fd, 
//                 EPOLL_CTL_ADD, 
//                 intrest_fd, 
//                 cur_stack_epoll_event
//         );

//         srv_ctx->epoll_fd_queue->i = srv_ctx->epoll_fd_queue->i + 1;
// }

static int accept_conn(int tcpfd)
{
        int ret = 0;

        // ret = accept(tcpfd, )

        return 0;
}

static int server_run_worker(struct server_ctx *srv_ctx, struct epoll_event *event_list)
{
        int event_count = 0;
        int ret = 0;

        struct epoll_event ev;
        struct sockaddr sockaddr;
        socklen_t socklen = sizeof(sockaddr);

        ev.events = EPOLLIN;

        epoll_ctl(srv_ctx->epoll_fd, EPOLL_CTL_ADD, srv_ctx->tcpfd, &ev);

        while (!*srv_ctx->need_exit_ptr) {
                event_count = epoll_wait(
                        srv_ctx->epoll_fd, 
                        event_list, MAX_ACCEPT_WORKER, 
                        20);


                // printf("%d\n", event_count);
                if (event_count > 0) {
                        for(int i = 0; i < event_count; i++) {
                                ret = accept(event_list[i].data.fd, &sockaddr, &socklen);
                                write(ret, "ok\n", 4);
                                printf("%d\n", event_list[i].data.fd);
                                perror("accept");

                                close(ret);

                                return 0;
                        }
                }
                        

                

        }

        return 0;
}

static void init_fd_sockaddr(struct fd_sockaddr_list *fdsocklist)
{
        fdsocklist->list = (struct _fd_sockaddr_list*)malloc(sizeof(struct _fd_sockaddr_list) * 1);
        fdsocklist->size = 1;
}

/* todo: insert active fd and sockaddr, so we can use that in other thread */
static int add_fd_sockaddr(struct fd_sockaddr_list *fdsocklist, int fd, struct sockaddr *sockaddr)
{
        /* init mem */
        fdsocklist->list[fdsocklist->size - 1].sockaddr = malloc(sizeof(struct sockaddr));

        memcpy(&fdsocklist->list[fdsocklist->size - 1].sockaddr, sockaddr, sizeof(struct sockaddr));
        fdsocklist->list[fdsocklist->size - 1].fd = fd;
        fdsocklist->list[fdsocklist->size - 1].is_active = 1;

        
        fdsocklist->size = fdsocklist->size + 1;
        void* dummymem = realloc(fdsocklist->list, sizeof(struct _fd_sockaddr_list) * fdsocklist->size);

        if (dummymem == NULL) {
                perror("realloc");

                return -1;
        } 

        fdsocklist->list = dummymem;

        return 0;
}

static struct sockaddr* get_by_fd_sockaddr(struct fd_sockaddr_list *fdsocklist, int fd_num)
{
        for(int i = 0; i < fdsocklist->size; i++) {
                if (fdsocklist->list[i].is_active == 1 && fdsocklist->list[i].fd == fd_num) {
                        return fdsocklist->list[i].sockaddr;
                }
        }

        return NULL;
}

static int free_fd_sockaddr(struct fd_sockaddr_list *fdsocklist)
{
        free(fdsocklist->list);
}


static int install_acceptfd_to_epoll(struct server_ctx *srv_ctx, int acceptfd)
{
        struct epoll_event ev;

        ev.data.fd = acceptfd;
        ev.events = EPOLLIN;
        
        epoll_ctl(srv_ctx->epoll_recv_fd, EPOLL_CTL_ADD, acceptfd, &ev);
        /* start long poll, append accept fd into */
        return 0;
}

static void* start_long_poll(void *srv_ctx_voidptr) {

        struct server_ctx *srv_ctx = (struct server_ctx*)srv_ctx_voidptr;
        int ret = 0;

        int n_ready_conn = 0;

        struct epoll_event tcpfd_event_list[MAX_ACCEPT_WORKER]; /* monitor tcpfd for accept request */
        struct epoll_event ev;
        
        ev.data.fd = srv_ctx->tcpfd;
        ev.events = EPOLLIN;

        struct sockaddr sockaddr;
        socklen_t socksize = sizeof(struct sockaddr);

        epoll_ctl(srv_ctx->epoll_fd, EPOLL_CTL_ADD, srv_ctx->tcpfd, &ev);

        while(!g_need_exit) {
                n_ready_conn = epoll_wait(srv_ctx->epoll_fd, tcpfd_event_list, 
                                                MAX_ACCEPT_WORKER, 20);
                                                
                if (n_ready_conn > 0) {
                        for(int i = 0; i < n_ready_conn; i++) {
                                ret = accept(tcpfd_event_list[i].data.fd, &sockaddr, &socksize);

                                /* start adding accept fd into watchlist */
                                install_acceptfd_to_epoll(srv_ctx, ret);

                                /* self note: add mutex */
                                add_fd_sockaddr(srv_ctx->fd_sockaddr_list, ret, &sockaddr);
                                
                        }
                }

                printf("ret %d\n", n_ready_conn);
                // server_accept_to_epoll(srv_ctx, tcpfd_event_list, &ev);
        }
}

static void* start_private_conn(void* accepted_fd)
{}

static void* start_long_poll_receiver(void *srv_ctx_voidptr)
{
        struct server_ctx *srv_ctx = (struct server_ctx*)srv_ctx_voidptr;

        int n_ready_read = 0;
        while(!g_need_exit) {
                n_ready_read = epoll_wait(srv_ctx->epoll_recv_fd, 
                                                srv_ctx->acceptfd_watchlist_event, EPOLL_ACCEPTFD_WATCHLIST_LEN, 20);

                if (n_ready_read > 0) {
                        for (int i = 0; i < n_ready_read; i++) {
                                get_by_fd_sockaddr(srv_ctx->fd_sockaddr_list, 
                                                srv_ctx->acceptfd_watchlist_event[i].data.fd);
                        }
                        
                        printf("event available to read %d\n", n_ready_read);
                } 
        }
}

static int enter_eventloop(struct server_ctx *srv_ctx)
{
        struct posix_thread_handler posix_thread_handler;
        struct fd_sockaddr_list fd_sockaddr_list;

        /* assign the pointer */
        srv_ctx->fd_sockaddr_list = &fd_sockaddr_list;

        /* init shared resource between thread */
        init_fd_sockaddr(&fd_sockaddr_list);

        /* appended from srv_ctx->epoll_recv_fd
         * warn: located on stack
         */
        struct epoll_event acceptfd_watchlist_event[EPOLL_ACCEPTFD_WATCHLIST_LEN];
        srv_ctx->acceptfd_watchlist_event = acceptfd_watchlist_event;
        
        setup_epoll(srv_ctx);
        
        pthread_create(&posix_thread_handler.poll_thread.pthread, NULL, start_long_poll, (void*)srv_ctx);
        /* set state to 1 */
        posix_thread_handler.poll_thread.state = 1;

        /* start our second receiver */
        pthread_create(&posix_thread_handler.poll_recv_thread.pthread, NULL, start_long_poll_receiver, (void*)srv_ctx);
        posix_thread_handler.poll_recv_thread.state = 1;

        /* start busy wait */
        while(!g_need_exit) {
                usleep(200);
        }
        close(srv_ctx->epoll_fd);     
        close(srv_ctx->epoll_recv_fd);
        free_fd_sockaddr(&fd_sockaddr_list);

        return 0;
}

static int main_server(struct runtime_opts *r_opts)
{
        int ret = 0;
        struct server_ctx *srv_ctx = (struct server_ctx*)malloc(sizeof(struct server_ctx));
        memset(srv_ctx, 0, sizeof(struct server_ctx));

        struct sockaddr_storage ss_addr;

        /* link our ptr */
        srv_ctx->need_exit_ptr = &g_need_exit;

        review_config(r_opts);

        if (setup_addr_storage(&ss_addr, r_opts) == -1) {
                fprintf(stderr, "Invalid ip format\n");
        }

        if ((ret = create_sock_ret_fd(&ss_addr)) == -1) {
                fprintf(stderr, "socket failed\n");
        }

        srv_ctx->tcpfd = ret;

        if ((ret = server_reg_sigaction()) ==  -1) {
                fprintf(stderr, "signal handler failed\n");
        }


        printf("server listening on %s:%d\n", r_opts->addr, r_opts->listenport);

        if ((ret = enter_eventloop(srv_ctx) == -1)) {
                fprintf(stderr, "error eventloop\n");
        }
        

        r_opts_clean(r_opts);
        close(ret);
        free(srv_ctx);

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