#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
/* create connection in C */

int main(int argc, char *argv)
{
        int ret = 0;

        int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(struct sockaddr_in));

        serv_addr.sin_addr.s_addr = inet_addr("192.168.2.2");
        serv_addr.sin_port = htons(3080);
        serv_addr.sin_family = AF_INET;

        socklen_t len = sizeof(struct sockaddr_in);
        ret = connect(tcpfd, (struct sockaddr*)&serv_addr, len);

        if (ret == -1) {
                perror("connect()");
                return -1;
        }

        char buf[4096];
        static const char sendbuf[] = "GET / HTTP/1.1\r\n\r\n";

        int buflen = strlen(sendbuf);

        write(tcpfd, sendbuf, buflen);
        recv(tcpfd, buf, 4096, 0);

        printf("%s\n", buf);

        close(tcpfd);
        return -1;
}