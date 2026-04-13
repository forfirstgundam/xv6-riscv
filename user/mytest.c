#include "kernel/types.h"
#include "user/user.h"

int main(void)
{
    int pid;

    printf("meminfo = %d\n", (int)meminfo());

    printf("my pid = %d\n", getpid());
    printf("default nice = %d\n", getnice(getpid()));

    printf("setnice(getpid(), 10) -> %d\n", setnice(getpid(), 10));
    printf("new nice = %d\n", getnice(getpid()));

    printf("setnice(getpid(), -1) -> %d\n", setnice(getpid(), -1));
    printf("setnice(getpid(), 40) -> %d\n", setnice(getpid(), 40));
    printf("setnice(9999, 10) -> %d\n", setnice(9999, 10));

    printf("getnice(9999) -> %d\n", getnice(9999));

    printf("all processes:\n");
    ps(0);

    printf("ps(9999): should print nothing below\n");
    ps(9999);
    printf("done ps(9999)\n");

    printf("waitpid(9999) -> %d\n", waitpid(9999));

    pid = fork();
    if (pid == 0)
    {
        printf("child pid = %d\n", getpid());
        printf("child nice = %d\n", getnice(getpid()));
        exit(0);
    }
    else
    {
        printf("waitpid(%d) -> %d\n", pid, waitpid(pid));
    }

    exit(0);
}