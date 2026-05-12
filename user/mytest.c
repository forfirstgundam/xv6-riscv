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

void print_result(char *name, int ok)
{
    if (ok)
        printf("[OK] %s\n", name);
    else
        printf("[FAIL] %s\n", name);
}

int cmp_bytes(char *a, char *b, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (a[i] != b[i])
            return 0;
    }

    return 1;
}

void test_two_page_anon(void)
{
    uint64 a;
    int before, after_mmap, after_touch, after_munmap;
    int *p1;
    int *p2;

    printf("\n[1] two-page anonymous lazy mmap\n");

    before = freemem();

    a = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    after_mmap = freemem();

    p1 = (int *)a;
    p2 = (int *)(a + 4096);

    *p1 = 1111;
    *p2 = 2222;

    after_touch = freemem();

    print_result("mmap returned MMAPBASE", a == MMAPBASE);
    print_result("lazy mmap did not allocate immediately", before == after_mmap);
    print_result("both pages readable/writable", *p1 == 1111 && *p2 == 2222);
    print_result("touching pages allocated memory", after_touch < after_mmap);

    print_result("munmap succeeds", munmap(a) == 1);

    after_munmap = freemem();
    print_result("munmap restored freemem", after_munmap == before);
}

void test_file_offset(void)
{
    int fd;
    int i;
    int n;
    char tmp[512];
    char expected[32];
    uint64 a;
    char *p;

    printf("\n[2] file mmap with offset 4096\n");

    fd = open("README", O_RDONLY);
    if (fd < 0)
    {
        printf("[FAIL] open README\n");
        return;
    }

    for (i = 0; i < 8; i++)
    {
        n = read(fd, tmp, sizeof(tmp));
        if (n != sizeof(tmp))
        {
            printf("[SKIP] README shorter than 4096 bytes\n");
            close(fd);
            return;
        }
    }

    n = read(fd, expected, sizeof(expected));
    close(fd);

    if (n <= 0)
    {
        printf("[SKIP] README has no data after offset 4096\n");
        return;
    }

    fd = open("README", O_RDONLY);
    if (fd < 0)
    {
        printf("[FAIL] reopen README\n");
        return;
    }

    a = mmap(0, 4096, PROT_READ, 0, fd, 4096);
    close(fd);

    if (a == 0)
    {
        printf("[FAIL] mmap file offset returned 0\n");
        return;
    }

    p = (char *)a;

    print_result("mapped file offset contents match", cmp_bytes(p, expected, n));
    print_result("munmap succeeds", munmap(a) == 1);
}

void test_fork_anon(void)
{
    uint64 a;
    int *p;
    int pid;

    printf("\n[3] fork with anonymous mmap\n");

    a = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (a == 0)
    {
        printf("[FAIL] mmap anon populate\n");
        return;
    }

    p = (int *)a;
    *p = 3333;

    pid = fork();

    if (pid < 0)
    {
        printf("[FAIL] fork failed\n");
        munmap(a);
        return;
    }

    if (pid == 0)
    {
        if (*p == 3333)
            printf("[OK] child sees parent anonymous mmap contents\n");
        else
            printf("[FAIL] child anonymous mmap contents wrong: %d\n", *p);

        *p = 4444;
        printf("[OK] child wrote anonymous mmap\n");
        exit(0);
    }

    wait(0);

    print_result("parent still sees original anonymous value", *p == 3333);
    print_result("munmap succeeds", munmap(a) == 1);
}

void test_fork_file(void)
{
    int fd;
    uint64 a;
    char *p;
    int pid;

    printf("\n[4] fork with file mmap\n");

    fd = open("README", O_RDONLY);
    if (fd < 0)
    {
        printf("[FAIL] open README\n");
        return;
    }

    a = mmap(0, 4096, PROT_READ, MAP_POPULATE, fd, 0);
    close(fd);

    if (a == 0)
    {
        printf("[FAIL] file mmap populate\n");
        return;
    }

    p = (char *)a;

    pid = fork();

    if (pid < 0)
    {
        printf("[FAIL] fork failed\n");
        munmap(a);
        return;
    }

    if (pid == 0)
    {
        printf("child first 32 chars: ");
        for (int i = 0; i < 32; i++)
            printf("%c", p[i]);
        printf("\n");

        if (p[0] == 'x' && p[1] == 'v' && p[2] == '6')
            printf("[OK] child sees file mmap contents\n");
        else
            printf("[FAIL] child file mmap contents wrong\n");

        exit(0);
    }

    wait(0);

    print_result("parent file mmap still valid", p[0] == 'x' && p[1] == 'v' && p[2] == '6');
    print_result("munmap succeeds", munmap(a) == 1);
}

void test_invalid_args(void)
{
    int fd;
    uint64 a;

    printf("\n[5] invalid argument cases\n");

    a = mmap(1, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    print_result("unaligned mmap addr returns 0", a == 0);

    a = mmap(0, 123, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    print_result("unaligned length returns 0", a == 0);

    a = mmap(0, 4096, PROT_READ, 0, -1, 0);
    print_result("file mmap with fd -1 returns 0", a == 0);

    fd = open("README", O_RDONLY);
    if (fd >= 0)
    {
        a = mmap(0, 4096, PROT_READ | PROT_WRITE, 0, fd, 0);
        print_result("write prot on read-only fd returns 0", a == 0);
        close(fd);
    }

    print_result("munmap unaligned returns -1", munmap(1) == -1);
    print_result("munmap nonexistent returns -1", munmap(MMAPBASE + 0x200000) == -1);
}

void test_readonly_write_fault(void)
{
    int fd;
    uint64 a;
    char *p;
    int pid;

    printf("\n[6] write fault on read-only mapping\n");

    fd = open("README", O_RDONLY);
    if (fd < 0)
    {
        printf("[FAIL] open README\n");
        return;
    }

    a = mmap(0, 4096, PROT_READ, 0, fd, 0);
    close(fd);

    if (a == 0)
    {
        printf("[FAIL] readonly mmap\n");
        return;
    }

    pid = fork();

    if (pid < 0)
    {
        printf("[FAIL] fork failed\n");
        munmap(a);
        return;
    }

    if (pid == 0)
    {
        p = (char *)a;
        p[0] = 'Z';
        printf("[FAIL] child survived write to read-only mmap\n");
        exit(0);
    }

    wait(0);
    printf("[OK] parent survived child read-only write-fault test\n");

    munmap(a);
}

void test_exit_cleanup_child(void)
{
    int before;
    int after;
    int pid;

    printf("\n[7] exit cleanup without munmap\n");

    before = freemem();

    pid = fork();

    if (pid < 0)
    {
        printf("[FAIL] fork failed\n");
        return;
    }

    if (pid == 0)
    {
        uint64 a;
        int *p;

        a = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        p = (int *)a;
        *p = 7777;

        // Intentionally no munmap().
        exit(0);
    }

    wait(0);

    after = freemem();

    print_result("child exit cleaned mmap pages", after == before);
}

int main(void)
{
    printf("---- Project 3 final mmap tests ----\n");

    test_two_page_anon();
    test_file_offset();
    test_fork_anon();
    test_fork_file();
    test_invalid_args();
    test_readonly_write_fault();
    test_exit_cleanup_child();

    printf("\n---- done ----\n");
    exit(0);
}