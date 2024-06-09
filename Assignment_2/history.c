#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
    char buffer[512]; 
    int fd1;
    long int n1;

    // open the file sh_history for reading
    if ((fd1 = open("sh_history", O_CREATE | O_RDONLY)) < 0 )
    {
        printf("file open/create problem");  
        exit(1);
    }

    // Print the content of the file to the screen
    while ((n1 = read (fd1, buffer, 512)) > 0) // While the content of fd1 is not empty yet, keep reading until it is empty
    {
        //return value of write() is the number of bytes actually written, so compare it with the number of bytes read from fd1
        if (write (1, buffer, n1) != n1)
        {
            printf("writing problem"); 
            exit(3);
        }
    }

    close(fd1);
    exit(0);
}
