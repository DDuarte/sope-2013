#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char const *argv[])
{
    pid_t pid = fork();

    if (pid == 0)
    {
        while(getppid() != 1) { }
        write(STDOUT_FILENO, "world !\n", strlen("world !\n"));
    }
    else
    {
        write(STDOUT_FILENO, "Hello ", strlen("Hello "));
    }

    return 0;
}