#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
        pid_t parentpid = getpid();
        
        pid_t child_pid = fork();

        if (child_pid == 0) {
                
                sleep(10);
                printf("child process created\n");
                _exit(0);
        } else {
                printf("this is parent\n");

                int status;
                wait(&status);
                sleep(20);
        }
        
}