// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define STEAL_NUMBER 200
void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// 各个CPU的空闲页列表以及保护锁
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  char lockname[8];
  for(int i = 0;i < NCPU; i++) {
    snprintf(lockname, sizeof(lockname), "kmem_%d", i);
    initlock(&kmem[i].lock, lockname);
  }
  freerange(end, (void*)PHYSTOP);
}


void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}




void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();  // 关中断
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();  //开中断
}

void *
kalloc(void)
{
  struct run *r;

  push_off();// 关中断
  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else {
    int another_id;  
    // 遍历所有CPU的空闲列表
    for(another_id = 0; another_id < NCPU; ++another_id) {
      if(another_id == id)
        continue;
      acquire(&kmem[another_id].lock);
      r = kmem[another_id].freelist;
      if(r) {
        kmem[another_id].freelist = r->next;
        release(&kmem[another_id].lock);
        break;
      }
      release(&kmem[another_id].lock);
    }
  }
  release(&kmem[id].lock);
  pop_off();  //开中断

  if(r)
    memset((char*)r, 5, PGSIZE); 
  return (void*)r;
}

