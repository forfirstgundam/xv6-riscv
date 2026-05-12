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
    int before, after_mmap, after_touch;
    int *p;

    printf("---- project 3 test ----\n");

    before = freemem();

    a = mmap(0, 4096, PROT_READ | PROT_WRITE,
             MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

    after_mmap = freemem();

    printf("mmap returned: 0x%lx\n", a);
    printf("before mmap: %d\n", before);
    printf("after mmap: %d\n", after_mmap);

    p = (int *)a;
    *p = 777;
    printf("*p = %d\n", *p);

    after_touch = freemem();
    printf("after touch: %d\n", after_touch);

    printf("munmap -> %d\n", munmap(a));
    printf("after munmap: %d\n", freemem());

    exit(0);
}