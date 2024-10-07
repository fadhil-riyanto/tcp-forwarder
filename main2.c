
#include <asm-generic/socket.h>
#include <signal.h>
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
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>

#include "config.h"

#define dbgchr(x) log_info("%c", x)

#define MAX_CLIENTS 10; 
#define FD_SOCKADDR_DBG(x) log_info(                    \
        "fd: %d; state: %d", x.fd, x.is_active              \
);

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

struct posix_thread_poll_thread {
        pthread_t pthread;
        int state;
};

struct posix_thread_handler
{
        struct posix_thread_poll_thread poll_thread;
        struct posix_thread_poll_thread poll_recv_thread;
        struct posix_thread_poll_thread gc_eventloop; /* currenly unused */
};


struct _fd_sockaddr_list {
        struct sockaddr_in sockaddr;
        int fd;
        int is_active; /* gate, allow rewrite or not */
};

struct fd_sockaddr_list {
        pthread_mutex_t fd_sockaddr_lock;
        struct _fd_sockaddr_list *list;
};

/* thread pool section */

struct _th_pool {
        int handled_fd;
        short is_active;
        short need_join;

        pthread_t th;
};

struct th_pool {
        pthread_mutex_t th_pool_mutex;
        struct _th_pool *th_pool;
        int size;
};

struct server_ctx {
        /* our tcp-fd */
        int tcpfd;

        /* handle tcp poll */
        int epoll_fd;

        /* monitor client accept */
        int epoll_recv_fd;

        /* handle accept fd appended by epoll_ctl */
        struct epoll_event *acceptfd_watchlist_event;

        /* hold stack ptr from enter_eventloop func */
        struct fd_sockaddr_list *fd_sockaddr_list;

        struct th_pool *th_pool;
        
        struct epoll_fd_queue *epoll_fd_queue; /* probably unused */

        volatile int *need_exit_ptr;
};

struct start_private_conn_details {
        struct server_ctx *srv_ctx;
        struct sockaddr_in sockdata;
        int acceptfd;
};

/* socks5 struct */
struct socks5_client_hello {
        u_int8_t ver;
        u_int8_t nmethods;
        u_int8_t methods;   
};

struct socks5_server_hello {
        u_int8_t ver;
        u_int8_t method;   
};

struct socks5_client_req {
        u_int8_t ver;
        u_int8_t cmd;   
        u_int8_t rsv;   
        u_int8_t atyp;   
        u_int8_t* dst_addr;   
        u_int16_t dst_port;
        
};

struct socks5_server_reply {
        u_int8_t ver;
        u_int8_t rep;
        u_int8_t reserved;
        u_int8_t atyp;
        u_int8_t* bind_addr;
        u_int16_t bind_port;
};

enum SOCKS5_CMD {
        SOCKS_CONNECT,
        SOCKS_BIND,
        SOCKS_UDP
};

enum SOCKS5_ADDRTYPE {
        SOCKS_IN,
        SOCKS_IN6,
        SOCKS_DOMAIN
};

struct next_req_ipv4 {
        u_int8_t version;
        u_int8_t cmd;
        u_int8_t reserved;
        u_int8_t atyp;
        u_int8_t dest[4];
        uint16_t port;
};

struct socks5_session {
        int is_auth;
};

struct fd_bridge {
        int client_fd;
        int target_fd;

        pthread_mutex_t mutex;
        u_int8_t need_exit;

        pthread_t client2srv_pthread;
        pthread_t srv2client_pthread;
        
};

volatile int g_need_exit = 0;

static void review_config(struct runtime_opts *r_opts)
{
        printf("socks5 server listen at %u\n", r_opts->listenport);
}

static void r_opts_clean(struct runtime_opts *r_opts)
{
        free(r_opts->addr);
}

static int setup_addr_storage(struct sockaddr_storage *ss_addr, 
                              struct runtime_opts *r_opts)
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

        if (signum == SIGINT) {
                g_need_exit = 1;
        }


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

        ret = sigaction(SIGPIPE, &sa, NULL);

        return 0;

}


static void setup_epoll(struct server_ctx *srv_ctx)
{
        srv_ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        srv_ctx->epoll_recv_fd = epoll_create(EPOLL_CLOEXEC);
}


static void free_fd_sockaddr(struct fd_sockaddr_list *fdsocklist)
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


static int uninstall_acceptfd_from_epoll(struct server_ctx *srv_ctx, int acceptfd)
{
        struct epoll_event ev;

        ev.data.fd = acceptfd;
        ev.events = EPOLLIN;
        
        epoll_ctl(srv_ctx->epoll_recv_fd, EPOLL_CTL_DEL, acceptfd, &ev);
        /* start long poll, append accept fd into */
        return 0;
}

/* init fd2sockaddr function */

static struct _fd_sockaddr_list* fd_sockaddr_list_ptr_init() 
{
        void* ptr = malloc(sizeof(struct _fd_sockaddr_list) * FREE_THREAD_ALLOC);
        memset(ptr, 0, (sizeof(struct _fd_sockaddr_list) * FREE_THREAD_ALLOC));

        return ptr;
}

static int fd_sockaddr_list_link(struct fd_sockaddr_list *fd_sockaddr_list,
        struct sockaddr_in sockaddr_in, int fd) 
{
        int ret = 0;

        for(int i = 0; i < FREE_THREAD_ALLOC; i++) {

                pthread_mutex_lock(&fd_sockaddr_list->fd_sockaddr_lock);

                if (fd_sockaddr_list->list[i].is_active == 0) {
                        fd_sockaddr_list->list[i].is_active = 1;
                        fd_sockaddr_list->list[i].sockaddr = sockaddr_in;
                        fd_sockaddr_list->list[i].fd = fd;

                        ret = 1;
                }

                pthread_mutex_unlock(&fd_sockaddr_list->fd_sockaddr_lock);
        }

        return ret;
}


static struct sockaddr_in* fd_sockaddr_list_get(struct fd_sockaddr_list *fd_sockaddr_list,
        int fd) 
{
        int ret = 0;

        for(int i = 0; i < FREE_THREAD_ALLOC; i++) {

                if (fd_sockaddr_list->list[i].fd == fd && fd_sockaddr_list->list[i].is_active == 1) {
                        return &fd_sockaddr_list->list[i].sockaddr;
                }
        }

        return NULL;
}


static int fd_sockaddr_list_del(struct fd_sockaddr_list fd_sockaddr_list,
        int fd) 
{
        int ret = 0;

        for(int i = 0; i < FREE_THREAD_ALLOC; i++) {

                pthread_mutex_lock(&fd_sockaddr_list.fd_sockaddr_lock);

                if (fd_sockaddr_list.list[i].is_active == 1 && fd_sockaddr_list.list[i].fd == fd) {
                        fd_sockaddr_list.list[i].is_active = 0;

                        ret = 1;
                }

                pthread_mutex_unlock(&fd_sockaddr_list.fd_sockaddr_lock);
        }

        return ret;
}

static int init_th_for_fd(struct th_pool *thpool, int fd)
{
        
        
        for(int i = 0; i < FREE_THREAD_ALLOC; i++) {
                if (thpool->th_pool[i].is_active == 0 && thpool->th_pool[i].need_join == 0) {
                        pthread_mutex_lock(&thpool->th_pool_mutex);

                        thpool->th_pool[i].is_active = 1;
                        thpool->th_pool[i].need_join = 0;
                        thpool->th_pool[i].handled_fd = fd;

                        thpool->size = thpool->size + 1; /* probably unused */
                        pthread_mutex_unlock(&thpool->th_pool_mutex);
                        

                        /* alloc here */
                        return i;
                }
        }

        

        

        return -1;
}

static void uninst_th_for_fd(struct th_pool *thpool, int fd)
{
        

        for(int i = 0; i < FREE_THREAD_ALLOC; i++) {
                if (thpool->th_pool[i].handled_fd == fd && thpool->th_pool[i].need_join == 1) {
                        pthread_join(thpool->th_pool[i].th, NULL);

                        pthread_mutex_lock(&thpool->th_pool_mutex);
                        thpool->th_pool[i].is_active = 0;
                        thpool->th_pool[i].need_join = 0;
                        thpool->size = thpool->size - 1;
                        pthread_mutex_unlock(&thpool->th_pool_mutex);

                }
        }

        
}

static inline char* cmd2str(int cmd)
{
        if (cmd == 1) {
                return "connect";
        }

        if (cmd == 2) {
                return "bind";
        }

        if (cmd == 3) {
                return "udp";
        }
}

static inline char* ip2str(int cmd)
{
        if (cmd == 1) {
                return "IPV4";
        }

        if (cmd == 3) {
                return "DOMAIN";
        }

        if (cmd == 4) {
                return "IPV6";
        }
}

static int socks5_handshake(int fd, char* buf, struct socks5_session *socks5_session)
{
        struct socks5_client_hello *c_hello = (struct socks5_client_hello*)buf;
        printf("SOCKS_HANDSHAKE version: %d; nmethods: %d; methods: %d\n", c_hello->ver, c_hello->nmethods, c_hello->methods);

        struct socks5_server_hello s_hello;
        s_hello.ver = 5;
        s_hello.method = 0;
        send(fd, (void*)&s_hello, sizeof(struct socks5_server_hello), 0);

        socks5_session->is_auth = 1;
}

// static int socks5_send_connstate(int fd, u_int8_t state, u_int8_t atyp, u_int8_t *addr, uint16_t port)
// {
//         u_int8_t emergencyipv4[4] = {
//                 addr[0], addr[1], addr[2], addr[3]
//         };

//         int ret = 0;
//         struct socks5_server_reply s_state;
//         s_state.ver = 5;
//         s_state.rep = state;
//         s_state.reserved = 0;
//         s_state.atyp = atyp;
//         s_state.bind_addr = emergencyipv4;
//         s_state.bind_port = port;

//         ret = send(fd, (void*)&s_state, sizeof(struct socks5_server_reply), 0);
//         if (ret == -1) {
//                 return -1;
//         }
//         return 0;

// }

// static int socks5_send_connstate(int fd, u_int8_t state, u_int8_t atyp, u_int8_t *addr, uint16_t port)
// {
//         int ret = 0;
//         size_t addr_off, total_send_len;
//         char buf[128];

//         struct socks5_server_reply s_state;
//         s_state.ver = 5;
//         s_state.rep = state;
//         s_state.reserved = 0;
//         s_state.atyp = atyp;

//         addr_off = offsetof(struct socks5_server_reply, bind_addr);
//         memcpy(buf, &s_state, addr_off);
//         memcpy(buf + addr_off, addr, 4);
//         memcpy(buf + addr_off + 4, &port, 2);
//         total_send_len = addr_off + 4 + 2;

//         ret = send(fd, buf, total_send_len, 0);
//         if (ret == -1) {
//                 return -1;
//         }
//         return 0;

// }

static int socks5_send_connstate(int fd, u_int8_t state, u_int8_t atyp, u_int8_t *addr, uint16_t port)
{
        int ret = 0;
        size_t addr_off, total_send_len;
        char buf[128];

        struct socks5_server_reply s_state;
        s_state.ver = 5;
        s_state.rep = state;
        s_state.reserved = 0;
        s_state.atyp = atyp;

        memcpy(buf, &s_state, 4);
        memcpy(buf + 4, addr, 4);
        memcpy(buf + 4 + 4, &port, 2);
        total_send_len = 4 + 4 + 2;

        ret = send(fd, buf, total_send_len, 0);
        if (ret == -1) {
                return -1;
        }
        return 0;

}

// static int debug_tcp(int fd)
// {
        
// }

static int start_unpack_packet(int fd, void* reserved, struct socks5_session *socks5_session)
{
        int ret = 0;

        

        if (socks5_session->is_auth == 0) {
                char buf[3];
                ret = read(fd, buf, 3);
                if (ret == 0) {
                        
                        return 0;
                }
                socks5_handshake(fd, buf, socks5_session);
        } else {
                // return 1;
                u_int8_t buf[4096];
                // struct next_req req_to;
                
                memset(buf, 0, 4096);
                memset(buf, 0, 4096);
                ret = read(fd, buf, 4096);
                
                if (ret == 0) {
                        return 0;
                }

                printf("debug %d\n", buf[0]);
               
        }

        return 1;
}

static int create_server2server_conn(int *fdptr, int atyp, u_int8_t *addr, u_int16_t port)
{
        int ret = 0;

        int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(struct sockaddr_in));

        if (atyp == 1) {
                char* buf = malloc(15);
                memset(buf, 0, 15);
                sprintf(buf, "%d.%d.%d.%d", (u_int8_t)addr[0], (u_int8_t)addr[1], (u_int8_t)addr[2], (u_int8_t)addr[3]);

                serv_addr.sin_addr.s_addr = inet_addr(buf);
                printf("contacting: %s\n", buf);
                free(buf);
        }
        
        serv_addr.sin_port = port;
        serv_addr.sin_family = AF_INET;

        socklen_t len = sizeof(struct sockaddr_in);
        ret = connect(tcpfd, (struct sockaddr*)&serv_addr, len);

        if (ret == -1) {
                perror("connect()");
                return -1;
        }

        *fdptr = tcpfd;

        return 0;
}

// static int read_fd(int fd, u_int8_t *buf, size_t sizeofbuf, size_t sizeof_read)
// {
//         int ret = 0;
// read_again:
//         ret = read(fd, buf, sizeofbuf);
//         if (ret < 0) {
//                 ret = errno;
//                 perror("read");
//                 close(fd);
//                 return ret;
//         }

//         if (ret == 0) {
//                 close(fd);
//                 return 0;
//         }
//         if (ret != 0) {

//                 goto read_again;
//         }

// }

/* return
 * 0: no problem
 * 1: conn closed by client
 * 2: recv client error
 * 3: client close connection
*/

static int start_exchange_data(int client_fd, int target_fd)
{
        int ret;
        int client_last_byte;
        int read_ret;
        int total_send_srv;
        u_int8_t buf[4096];
        u_int8_t srvbuf[4096];
        int total_srv_read = 0;
        const u_int8_t *bufptr;

        int sd, v;

        do {
                
                
                // fcntl(client_fd, F_SETFL, O_NONBLOCK);

                /* recv data from socket client */

readbuf:
                printf("start sending\n");
                memset(buf, 0, 4096);
                ret = recv(client_fd, buf, 4096, 0);
                client_last_byte = ret;

                if (ret == -1) {
                        ret = errno;

                        if (errno == 11) {
                                goto readbuf;
                        }
                        if (errno == 9) {
                                close(client_fd);
                                return 0;
                        }
                        perror("recv client");
                        printf("clientfd errno %d\n", errno);
                        return 2;
                } 

                if (ret == 0) {
                        close(client_fd);
                } else {
                        ret = send(target_fd, buf, ret, 0);

                        if (ret == -1) {
                                perror("send() client to srv error");
                                close(target_fd);
                                return 3;
                        }

                        int actuall_off = (client_last_byte - 1);
                        // printf("ended by newline %c aaa", (buf[actuall_off]));
                        if (buf[actuall_off - 3] == '\r' && buf[actuall_off - 2] == '\n' && buf[actuall_off - 1] == '\r' && buf[actuall_off] == '\n') {
                                /* pass*/
                                printf("ended by newline\n");
                        } else {
                                goto readbuf;
                        }
                        
                }

                // printf("recv from client: %d bytes\n", ret);
                // printf("recv data: %s\n", buf);
                
// do_send_target:
                // read_ret = ret;
                // // bufptr = buf;
                
                // ret = send(target_fd, buf, ret, 0);
                // if (ret == -1) {
                //         perror("send() target server");
                // }

                // if ((read_ret - ret) != 0) {

                //         *buf = *buf + ret;
                //         goto do_send_target;
                // }

                /* read response from server */


readbuf_server:
                memset(srvbuf, 0, 4096);
                *srvbuf = 0;
                ret = recv(target_fd, srvbuf, sizeof(srvbuf), 0);
                
                if (ret == -1) {
                        ret = errno;

                        if (errno == 11) {
                                goto readbuf_server;
                        }
                        if (errno == 9) {
                                close(client_fd);
                                return 0;
                        }
                        perror("recv server");
                        printf("serverfd errno %d\n", errno);
                        return 2;
                } 

                if (ret == 0) {
                        close(target_fd);
                } else {
                        // *srvbuf = *srvbuf + ret;
                        // total_srv_read += ret;
                        ret = send(client_fd, srvbuf, ret, MSG_NOSIGNAL);

                        if (ret == -1) {
                                perror("send() to client from srv");
                                close(client_fd);
                                return 3;
                        }
                        goto readbuf_server;
                }
                close(client_fd);
        } while (ret != 0);

        return 0;
}

/* todo
 * read from client
 * send it to the server
 * read again from client
 * send it to the server

 * didnt care what protocol used

 return 
 * -1 on error
*/


static int start_exchange_data1(int client_fd, int target_fd)
{

        u_int8_t buf[4096];
        int client_ret;
        int srv_ret;
        int general_ret;

start_read1:
        memset(buf, 0, 4096);
        printf("b1\n");
        
        client_ret = recv(client_fd, buf, 4096, 0);
        if (client_ret == -1) {
                perror("recv from client socks5");
        } 
        printf("%d\n", client_ret);
        
        if (client_ret != 0) {
                printf("b2\n");
                srv_ret = send(target_fd, buf, client_ret, 0);
                if (srv_ret == -1) {
                        perror("send from target server");
                        close(target_fd);
                }
        }

        memset(buf, 0, 4096);
        
        printf("b3\n");
        srv_ret = recv(target_fd, buf, 4096, 0);
        if (client_ret == -1) {
                perror("recv from target server");
        }

        printf("b4\n");
        client_ret = send(client_fd, buf, srv_ret, MSG_NOSIGNAL);
        if (client_ret == -1) {
                general_ret = errno;

                // if (general_ret == )
                perror("send to client socks5");
                close(client_fd);
                return -1;
        } 
        goto start_read1;
        
}

static void* start_exchange_data2_client2srv(void *fd_bridgeptr)
{
        struct fd_bridge *fd_bridge = (struct fd_bridge*)fd_bridgeptr;
        int client_ret;
        int srv_ret;
        u_int8_t buf[4096];

        while (1) {
                memset(buf, 0, 4096);
                client_ret = recv(fd_bridge->client_fd, buf, 4096, 0);
                if (client_ret == -1) {
                        perror("client2srv recv:");
                } else {
                        srv_ret = send(fd_bridge->target_fd, buf, client_ret, 0);
                        if (srv_ret == -1) {
                                perror("client2srv send:");
                        }
                }

                
                usleep(100);
                
        }

}

static void* start_exchange_data2_srv2client(void *fd_bridgeptr)
{
        struct fd_bridge *fd_bridge = (struct fd_bridge*)fd_bridgeptr;
        int client_ret;
        int srv_ret;
        u_int8_t buf[4096];

        while (1) {
                memset(buf, 0, 4096);
                client_ret = recv(fd_bridge->target_fd, buf, 4096, 0);
                if (client_ret == -1) {
                        perror("srv2client recv:");
                } else {
                        srv_ret = send(fd_bridge->client_fd, buf, client_ret, 0);
                        if (srv_ret == -1) {
                                perror("srv2client send:");
                        }
                }

                usleep(100);
        }
        
}

static int start_exchange_data2(int client_fd, int target_fd)
{
        struct fd_bridge fd_bridge;
        fd_bridge.need_exit = 0;
        fd_bridge.client_fd = client_fd;
        fd_bridge.target_fd = target_fd;

        pthread_mutex_init(&fd_bridge.mutex, NULL);

        pthread_create(&fd_bridge.client2srv_pthread, 
                0, start_exchange_data2_client2srv, (void*)&fd_bridge);

        pthread_create(&fd_bridge.srv2client_pthread, 
                0, start_exchange_data2_srv2client, (void*)&fd_bridge);

        while (!(fd_bridge.need_exit == 1)) {
                usleep(100);
        }
        

        pthread_mutex_destroy(&fd_bridge.mutex);

}

static int start_unpack_packet_no_epl(int fd, void* reserved, struct socks5_session *socks5_session)
{
        char buf[4096];
        int ret = 0;
        int exc_ret = 0;

        do {
                ret = read(fd, buf, 4096);
                if (ret == 0) {
                        return 0;
                }
                if (socks5_session->is_auth == 0) {
                        socks5_handshake(fd, buf, socks5_session);
                } else {
                        
                        if (buf[3] == 1) {
                                struct next_req_ipv4 *next_req = (struct next_req_ipv4*)buf;
                                int cur_conn_clientfd = 0;
                                
                                printf("SOCKS_REQ ver: %c; CMD: %s; type: %s; ip: %u.%u.%u.%u:%d\n", buf[0], 
                                        cmd2str(next_req->cmd), ip2str(next_req->atyp), next_req->dest[0], next_req->dest[1], next_req->dest[2], next_req->dest[3],
                                        ntohs(next_req->port));

                                ret = create_server2server_conn(&cur_conn_clientfd, next_req->atyp, next_req->dest, next_req->port);

                                if (ret == 0) {
                                        socks5_send_connstate(fd, 0, next_req->atyp, next_req->dest, 
                                                next_req->port);
                                        
                                        exc_ret = start_exchange_data2(fd, cur_conn_clientfd);
                                        if (exc_ret == 1) {
                                                close(cur_conn_clientfd);
                                                close(fd);
                                                return 0;
                                        }
                                }
                        }
                }
        }while (ret != 0);
}


static void* start_private_conn_no_epl(void *priv_conn_detailsptr)
{
        struct start_private_conn_details *priv_conn_details = (struct start_private_conn_details*)priv_conn_detailsptr;

        struct server_ctx *srv_ctx = priv_conn_details->srv_ctx;

        struct socks5_session socks5_session;
        socks5_session.is_auth = 0;

        int current_fd = priv_conn_details->acceptfd;
        int ret = start_unpack_packet_no_epl(current_fd, NULL, &socks5_session);
        if (ret == 0) {
                printf("connection closed\n");
                
        }
}

static void* start_private_conn(void *priv_conn_detailsptr)
{
        struct start_private_conn_details *priv_conn_details = (struct start_private_conn_details*)priv_conn_detailsptr;


        int n_ready_conn = 0;
        struct server_ctx *srv_ctx = priv_conn_details->srv_ctx;
        int current_fd = priv_conn_details->acceptfd;
        struct sockaddr_in current_sockdata = priv_conn_details->sockdata;
        struct socks5_session socks5_session;
        socks5_session.is_auth = 0;

        char ip_str[INET_ADDRSTRLEN];

        while (!g_need_exit) {
                
                n_ready_conn = epoll_wait(srv_ctx->epoll_recv_fd,  srv_ctx->acceptfd_watchlist_event, EPOLL_ACCEPTFD_WATCHLIST_LEN, 
                                                20);
                                                
                if (n_ready_conn > 0) {
                        
                        for (int i = 0; i < n_ready_conn; i++) {
                                
                                if (srv_ctx->acceptfd_watchlist_event[i].data.fd == current_fd && !g_need_exit) {
                                        

                                        int ret = start_unpack_packet(current_fd, NULL, &socks5_session);

                                        if (ret == 0) {
                                                // struct sockaddr_in *data = fd_sockaddr_list_get(srv_ctx->fd_sockaddr_list, current_fd);
                                                inet_ntop(AF_INET, &current_sockdata.sin_addr, ip_str, INET_ADDRSTRLEN);
                                                printf("closed %s\n", ip_str);
                                                close(current_fd);
                                                uninst_th_for_fd(srv_ctx->th_pool, current_fd);
                                        }
                                }
                        }
                }
        }
}

static void handle_user_max(int fd) 
{
        log_fatal("maximum connection reached");
        close(fd);
}

static void* start_long_poll(void *srv_ctx_voidptr) {

        struct server_ctx *srv_ctx = (struct server_ctx*)srv_ctx_voidptr;
        int ret = 0;

        struct start_private_conn_details start_private_conn2thread;
        start_private_conn2thread.srv_ctx = srv_ctx;

        int n_ready_conn = 0;
        char ip_str[INET_ADDRSTRLEN];

        struct epoll_event tcpfd_event_list[MAX_ACCEPT_WORKER]; /* monitor tcpfd for accept request */
        struct epoll_event ev;
        
        ev.data.fd = srv_ctx->tcpfd;
        ev.events = EPOLLIN;

        struct sockaddr_in sockaddr;
        socklen_t socksize = sizeof(struct sockaddr_in);

        epoll_ctl(srv_ctx->epoll_fd, EPOLL_CTL_ADD, srv_ctx->tcpfd, &ev);

        while(!g_need_exit) {
                n_ready_conn = epoll_wait(srv_ctx->epoll_fd, tcpfd_event_list, 
                                                MAX_ACCEPT_WORKER, 20);
                                                
                if (n_ready_conn > 0) {
                        for(int i = 0; i < n_ready_conn; i++) {

                                /* need call func*/
                                ret = accept(tcpfd_event_list[i].data.fd, 
                                        (struct sockaddr*)&sockaddr, &socksize);

                                /* start adding accept fd into watchlist */
                                install_acceptfd_to_epoll(srv_ctx, ret);
                                
                                fd_sockaddr_list_link(srv_ctx->fd_sockaddr_list, sockaddr, ret);

                                inet_ntop(AF_INET, &sockaddr.sin_addr, ip_str, INET_ADDRSTRLEN);
                                log_info("accepted [%d] %s", ret, ip_str);


                                /* generate thread */
                                int th_num = init_th_for_fd(srv_ctx->th_pool, ret);
                                if (th_num == -1) {
                                        handle_user_max(th_num);
                                } else {
                                        
                                        /* pass the data */
                                        start_private_conn2thread.acceptfd = ret;
                                        start_private_conn2thread.sockdata = sockaddr;

                                        pthread_create(
                                                &srv_ctx->th_pool->th_pool[th_num].th, NULL, 
                                                start_private_conn_no_epl, (void*)&start_private_conn2thread);
                                }
                        }
                }
                // server_accept_to_epoll(srv_ctx, tcpfd_event_list, &ev);
        }
}

/* probably unused */
static void* start_long_poll_receiver(void *srv_ctx_voidptr)
{
        struct server_ctx *srv_ctx = (struct server_ctx*)srv_ctx_voidptr;
        struct start_private_conn_details start_private_conn2thread;

        start_private_conn2thread.srv_ctx = srv_ctx;

        int n_ready_read = 0;
        while(!g_need_exit) {
                n_ready_read = epoll_wait(srv_ctx->epoll_recv_fd, 
                                                srv_ctx->acceptfd_watchlist_event, 
                                                EPOLL_ACCEPTFD_WATCHLIST_LEN, 
                                                20);

                if (n_ready_read < 0) {
                        perror("epoll_wait");
                } 

                if (n_ready_read > 0) {
                        for (int i = 0; i < n_ready_read; i++) {
                                // struct sockaddr_in *sock_gate = fd_sockaddr_list_get(srv_ctx->fd_sockaddr_list, 
                                //         srv_ctx->acceptfd_watchlist_event[i].data.fd);

                                // inet_ntop(AF_INET, &sock_gate->sin_addr, ip_str, INET_ADDRSTRLEN);
                                // printf("closed from %s\n", ip_str);
                                close(srv_ctx->acceptfd_watchlist_event[i].data.fd);
                        }
                        
                        printf("event available to read %d\n", n_ready_read);
                } 

                /* todo: run cleaner here */
        }
}

static void cleanup_eventloop_thread(struct posix_thread_handler *thhandler)
{
        pthread_join(thhandler->poll_thread.pthread, NULL);
        // pthread_join(thhandler->poll_recv_thread.pthread, NULL);
        
}

static int enter_eventloop(struct server_ctx *srv_ctx)
{
        struct posix_thread_handler posix_thread_handler;

        struct _fd_sockaddr_list *_fd_sockaddr_list = fd_sockaddr_list_ptr_init();

        /* assign the pointer */
        struct fd_sockaddr_list fd_sockaddr_list;
        pthread_mutex_init(&fd_sockaddr_list.fd_sockaddr_lock, NULL);
        fd_sockaddr_list.list = _fd_sockaddr_list;
        
        srv_ctx->fd_sockaddr_list = &fd_sockaddr_list;

        /* init thread pool */
        struct _th_pool *_th_pool = malloc(sizeof(struct _th_pool) * FREE_THREAD_ALLOC);
        memset(_th_pool, 0, sizeof(struct _th_pool) * FREE_THREAD_ALLOC);

        struct th_pool th_pool = {
                .th_pool = _th_pool,
                .size = 0,
        };

        pthread_mutex_init(&th_pool.th_pool_mutex, NULL);

        srv_ctx->th_pool = &th_pool;


        /* appended from srv_ctx->epoll_recv_fd
         * warn: located on stack
         */
        struct epoll_event acceptfd_watchlist_event[EPOLL_ACCEPTFD_WATCHLIST_LEN];
        srv_ctx->acceptfd_watchlist_event = acceptfd_watchlist_event;
        
        setup_epoll(srv_ctx);
        
        pthread_create(&posix_thread_handler.poll_thread.pthread, 
                        NULL, start_long_poll, (void*)srv_ctx);
        /* set state to 1 */
        posix_thread_handler.poll_thread.state = 1;

        /* start our second receiver */
        // pthread_create(&posix_thread_handler.poll_recv_thread.pthread, 
        //                 NULL, start_long_poll_receiver, (void*)srv_ctx);
        // /* set state to 1 */
        // posix_thread_handler.poll_recv_thread.state = 1;

        /* start busy wait */
        while(!g_need_exit) {
                usleep(200);
        }
        close(srv_ctx->epoll_fd);     
        close(srv_ctx->epoll_recv_fd);

        cleanup_eventloop_thread(&posix_thread_handler);
        
        free(_fd_sockaddr_list);
        free(_th_pool);
        // free(srv_ctx);

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