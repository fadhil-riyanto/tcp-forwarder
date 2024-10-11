
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static void* a(void* aa) {
        
}



int main()
{
        pthread_t abc;
        pthread_create(&abc, NULL, a, NULL);

        printf("ok\n");
        pthread_join(abc, NULL);
        printf("will i printed?\n");
}