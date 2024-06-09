#include <sys/wait.h>
#include "user.h"

int main(int argc, char* argv[])
{
    int pid;
    int timer;

    // Check if the number of cmd line arguments is correct
    if (argc < 2) 
    {
        printf("How to use: %s <command> <optional command line arguments>\n", argv[0]);
        exit(1);
    }

    // Start timer here
    timer = uptime(); 
    printf("Up Time: %d\n", timer);

    // Fork and let Child process do the exec() function
    pid = fork();
    if (pid < 0)
    {
        printf("fork failed\n");
    }

    // Child process will do the exec() starting from arg[1]. See the exec() function implementation by pressing F12 on 'exec' in VS code to trace
    if (pid == 0)
    {
        exec(argv[1], &argv[1]);
    }

    // Parent proces wait for child here
    wait(0);
    
    // End the timer here
    timer = uptime(); 
    printf("Up Time: %d\n", timer);

    // exit with code 5
    exit(5);
}