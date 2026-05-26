// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

#define NSWAPPAGE (SWAPMAX / (PGSIZE / BSIZE))

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// pa4: struct for page control
struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;

char *swap_bitmap;
struct spinlock swap_lock;

struct spinlock lru_lock;

// add function names
static int swap_alloc_slot(void);
static void *swapout(void);

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&lru_lock, "lru");
  initlock(&swap_lock, "swap");

  page_lru_head = 0;
  num_lru_pages = 0;
  num_free_pages = 0;

  freerange(end, (void*)PHYSTOP);

  swap_bitmap = kalloc();
  if(swap_bitmap == 0)
    panic("kinit: no swap bitmap");
  memset(swap_bitmap, 0, PGSIZE);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  num_free_pages++;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// pa4: kalloc function
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    num_free_pages--;
  }
  release(&kmem.lock);

  if(r == 0)
    r = (struct run *)swapout();

  if(r)
    memset((char*)r, 5, PGSIZE);

  return (void*)r;
}

static void
lru_insert(struct page *pg)
{
  struct page *tail;

  if(page_lru_head == 0) {
    pg->next = pg;
    pg->prev = pg;
    page_lru_head = pg;
  } else {
    tail = page_lru_head->prev;

    pg->next = page_lru_head;
    pg->prev = tail;

    tail->next = pg;
    page_lru_head->prev = pg;
  }

  num_lru_pages++;
}

void
lru_add(pagetable_t pagetable, uint64 va, uint64 pa)
{
  struct page *pg;

  if(pa >= PHYSTOP)
    panic("lru_add");

  pg = &pages[pa / PGSIZE];

  acquire(&lru_lock);

  // Already on LRU list.
  if(pg->pagetable != 0) {
    release(&lru_lock);
    return;
  }

  pg->pagetable = pagetable;
  pg->vaddr = (char *)PGROUNDDOWN(va);

  lru_insert(pg);

  release(&lru_lock);
}

static void
lru_detach_locked(struct page *pg)
{
  if(pg->pagetable == 0)
    return;

  if(pg->next == pg) {
    page_lru_head = 0;
  } else {
    pg->prev->next = pg->next;
    pg->next->prev = pg->prev;

    if(page_lru_head == pg)
      page_lru_head = pg->next;
  }

  pg->next = 0;
  pg->prev = 0;
  num_lru_pages--;
}

void
lru_remove(uint64 pa)
{
  struct page *pg;

  if(pa >= PHYSTOP)
    return;

  pg = &pages[pa / PGSIZE];

  acquire(&lru_lock);

  if(pg->pagetable == 0) {
    release(&lru_lock);
    return;
  }

  lru_detach_locked(pg);

  pg->pagetable = 0;
  pg->vaddr = 0;

  release(&lru_lock);
}

static void
lru_move_tail_locked(struct page *pg)
{
  struct page *tail;

  if(page_lru_head == 0 || pg->next == pg)
    return;

  lru_detach_locked(pg);

  if(page_lru_head == 0) {
    pg->next = pg;
    pg->prev = pg;
    page_lru_head = pg;
  } else {
    tail = page_lru_head->prev;

    pg->next = page_lru_head;
    pg->prev = tail;

    tail->next = pg;
    page_lru_head->prev = pg;
  }

  num_lru_pages++;
}

struct page *
lru_select_victim(void)
{
  struct page *pg;
  pte_t *pte;

  acquire(&lru_lock);

  while(page_lru_head != 0) {
    pg = page_lru_head;

    if(pg->pagetable == 0 || pg->vaddr == 0) {
      lru_detach_locked(pg);
      pg->pagetable = 0;
      pg->vaddr = 0;
      continue;
    }

    pte = walk(pg->pagetable, (uint64)pg->vaddr, 0);

    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) {
      lru_detach_locked(pg);
      pg->pagetable = 0;
      pg->vaddr = 0;
      continue;
    }

    if(*pte & PTE_A) {
      *pte &= ~PTE_A;
      lru_move_tail_locked(pg);
      continue;
    }

    // Found victim. Remove from LRU, but keep pagetable/vaddr
    // so swap-out code can update the PTE.
    lru_detach_locked(pg);

    release(&lru_lock);
    return pg;
  }

  release(&lru_lock);
  return 0;
}

static int
swap_alloc_slot(void)
{
  int i;
  int byte;
  int bit;

  acquire(&swap_lock);

  for(i = 0; i < NSWAPPAGE; i++) {
    byte = i / 8;
    bit = i % 8;

    if((swap_bitmap[byte] & (1 << bit)) == 0) {
      swap_bitmap[byte] |= (1 << bit);
      release(&swap_lock);
      return i;
    }
  }

  release(&swap_lock);
  return -1;
}

void
swap_free_slot(int slot)
{
  int byte;
  int bit;

  if(slot < 0 || slot >= NSWAPPAGE)
    return;

  byte = slot / 8;
  bit = slot % 8;

  acquire(&swap_lock);
  swap_bitmap[byte] &= ~(1 << bit);
  release(&swap_lock);
}

static void *
swapout(void)
{
  struct page *victim;
  pte_t *pte;
  uint64 pa;
  int slot;
  uint64 flags;

  victim = lru_select_victim();

  if(victim == 0) {
    printf("kalloc: OOM, no page in LRU list\n");
    return 0;
  }

  pte = walk(victim->pagetable, (uint64)victim->vaddr, 0);
  if(pte == 0 || (*pte & PTE_V) == 0) {
    printf("swapout: bad victim pte\n");
    victim->pagetable = 0;
    victim->vaddr = 0;
    return 0;
  }

  slot = swap_alloc_slot();
  if(slot < 0) {
    printf("kalloc: OOM, no swap slot\n");
    return 0;
  }

  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);

  swapwrite(pa, slot);

  // Store swap slot in PPN field, clear valid bit, mark as swapped.
  *pte = ((uint64)slot << 10) | ((flags & ~PTE_V) | PTE_SWAP);
  *pte &= ~PTE_A;

  sfence_vma();

  victim->pagetable = 0;
  victim->vaddr = 0;

  return (void *)pa;
}
