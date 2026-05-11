#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/syscall.h"

int main(void)
{
    uint64 a;
    int fd;

    printf("---- mmap bookkeeping test ----\n");

    a = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    printf("anon mmap returned: %lx\n", a);

    fd = open("README", O_RDONLY);
    if (fd < 0)
    {
        printf("open README failed\n");
        exit(1);
    }

    a = mmap(4096, 4096, PROT_READ, 0, fd, 0);
    printf("file mmap returned: %lx\n", a);

    close(fd);

    printf("freemem = %d\n", freemem());

    exit(0);
}