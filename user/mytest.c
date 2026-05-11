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

    printf("---- mmap page fault test ----\n");

    before = freemem();

    a = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    after_mmap = freemem();

    printf("mmap returned: 0x%lx\n", a);
    printf("before mmap: %d\n", before);
    printf("after mmap: %d\n", after_mmap);

    p = (int *)a;
    *p = 12345; // should cause page fault and allocate one page

    after_touch = freemem();

    printf("*p = %d\n", *p);
    printf("after touch: %d\n", after_touch);

    exit(0);
}