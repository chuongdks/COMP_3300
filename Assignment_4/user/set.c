#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
    int pid, priority;

    if(argc < 3)
    {
        printf("Not enough data. How to use: set 'pid number' 'priority number'\n");
        exit(-1);
    }

    // convert string to number
    pid = atoi(argv[1]);
    priority = atoi(argv[2]);

    if (priority < 0 || priority > 20)
    {
        printf("Invalid priority range. Choose from 0 - 20\n");
        exit(-1);
    }

    set(pid, priority);

    exit(0);
}
