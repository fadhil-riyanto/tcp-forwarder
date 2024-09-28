
#include <signal.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>

int *g_signal_need_exit;
int saved_ppid = 0;

void signal_notify(int signum)
{
        if (saved_ppid == getpid()) {
                
                printf("parent got signal %d\n", signum);
        }

        *g_signal_need_exit = 1;
        
}

static void install_signal(void)
{
        struct sigaction sa;

        sa.sa_handler = signal_notify;

        if (sigaction(SIGINT, &sa, NULL) == -1) {
                perror("sigaction");
        }
        
}

static void enter_eventloop(void)
{
        while(!g_signal_need_exit) {
                
        }
        printf("thread exit\n");

        _exit(0);
        
}

int main(void)
{
        g_signal_need_exit = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        *g_signal_need_exit = 0;


        saved_ppid = getpid();
        install_signal();

        for(int i = 0; i < 5; i++) {
                if (fork() == 0) {
                        enter_eventloop();
                        
                        /* raise(SIGUSR1); */
                        
                }
        }

        while(wait(NULL) > 0);

        munmap(g_signal_need_exit, sizeof(int));
}