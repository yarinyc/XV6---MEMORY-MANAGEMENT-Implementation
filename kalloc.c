// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct gloabl_meta_data gloabl_memory_meta_data; //task 4: export this variable so it's global in the system

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  uint referenceCounters[PHYSTOP/PGSIZE];    // reference counter for COW
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
  gloabl_memory_meta_data.total_system_pages = ((PGROUNDDOWN((uint)vend))-(PGROUNDUP((uint)vstart)))/PGSIZE;
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  gloabl_memory_meta_data.total_system_pages += ((PGROUNDDOWN((uint)vend))-(PGROUNDUP((uint)vstart)))/PGSIZE;
  kmem.use_lock = 1;
  gloabl_memory_meta_data.system_free_pages = gloabl_memory_meta_data.total_system_pages;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;
  if(((uint)v % PGSIZE) || (v < end) || (V2P(v) >= PHYSTOP)){
    panic("kfree"); 
    }

    if(kmem.use_lock){
      acquire(&kmem.lock);
    }

    if(kmem.use_lock && kmem.referenceCounters[(V2P(v) / PGSIZE)] == 0){
      cprintf("bad address 0x%x\n",v);
      panic("kfree: ref is 0 and tried to kfree v");
    }


    if(kmem.referenceCounters[(V2P(v) / PGSIZE)] > 0)
        kmem.referenceCounters[(V2P(v) / PGSIZE)]--;
    if(kmem.referenceCounters[(V2P(v) / PGSIZE)] == 0){
      memset(v, 1, PGSIZE);
      r = (struct run*)v;
      r->next = kmem.freelist;
      kmem.freelist = r;
      gloabl_memory_meta_data.system_free_pages++;
    }

    if(kmem.use_lock)
      release(&kmem.lock);
}
   
// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  
  if(kmem.use_lock){
    acquire(&kmem.lock);
  }
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    gloabl_memory_meta_data.system_free_pages--;
    kmem.referenceCounters[(V2P(r) / PGSIZE)] = 1; // ref is 1 for a new page
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

// increase the reference counter by 1 to the physical page pointed to by kernel virtual address v
void
incrementRef(char *v){
  if(V2P(v) < (uint)V2P(end) || V2P(v) >= PHYSTOP)
    panic("incrementRef");

  if(kmem.use_lock)
    acquire(&kmem.lock);
  kmem.referenceCounters[(V2P(v) / PGSIZE)]++;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// decrease the reference counter by 1 to the physical page pointed to by kernel virtual address v
void
decrementRef(char *v){
  if(V2P(v) < (uint)V2P(end) || V2P(v) >= PHYSTOP)
    panic("decrementRef");

  if(kmem.use_lock)
    acquire(&kmem.lock);
  kmem.referenceCounters[(V2P(v) / PGSIZE)]--;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// returns the current reference counter to the physical page pointed to by kernel virtual address v
uint
getRef(char *v){
  uint res;
  if(kmem.use_lock)
    acquire(&kmem.lock);
  res = kmem.referenceCounters[(V2P(v) / PGSIZE)];
  if(kmem.use_lock)
    release(&kmem.lock);
  return res;
}

// lock kmem.lock
void kmemLock(){
  acquire(&kmem.lock);
}
// unlock kmem.lock
void kmemRelease(){
  release(&kmem.lock);
}
