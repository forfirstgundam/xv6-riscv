#include "kernel/types.h"
#include "user/user.h"

int main(void)
{
    int pid;

    printf("meminfo = %d\n", (int)meminfo());

    printf("my pid = %d\n", getpid());
    printf("default nice = %d\n", getnice(getpid()));

    printf("setnice -> %d\n", setnice(getpid(), 10));
    printf("new nice = %d\n", getnice(getpid()));

    printf("all processes:\n");
    ps(0);

    pid = fork();
    if (pid == 0)
    {
        printf("child pid = %d\n", getpid());
        exit(0);
    }
    else
    {
        printf("waitpid(%d) -> %d\n", pid, waitpid(pid));
    }

    exit(0);
}