
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>

int main() 
{
        struct addrinfo req;
        void *ptr;

        memset(&req, 0, sizeof(req));

        struct addrinfo *res, *i;
        int ret = 0;

        char buf[NI_MAXHOST];

        req.ai_family = AF_UNSPEC;
        req.ai_socktype = SOCK_DGRAM;

        ret = getaddrinfo("fb.com", NULL, &req, &res);
        if (ret == 0) {
                while (res) {
                        memset(buf, 0, NI_MAXHOST);

                        if (res->ai_family == AF_INET6) {
                                ptr = &((struct sockaddr_in6*)res->ai_addr)->sin6_addr;
                        
                                inet_ntop(AF_INET6, ptr, buf, NI_MAXHOST);
                                printf("%s\n", buf);
                        } else if (res->ai_family == AF_INET) {
                                ptr = &((struct sockaddr_in*)res->ai_addr)->sin_addr;
                        
                                inet_ntop(AF_INET, ptr, buf, NI_MAXHOST);
                                printf("%s\n", buf);
                        }
                        

                        res = res->ai_next;
                }
        } else {
                printf("invalid %s\n", gai_strerror(ret));
        }
}
