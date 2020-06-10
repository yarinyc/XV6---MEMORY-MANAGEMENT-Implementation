#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

//global variables for freevm function - use the global one if current process is within freevm
int global_pgdir_flag = 0;
pde_t *global_pgdir;

//declarations for Task 1:
void addPageToRam(char * addr);
void removePageFromList(struct page_link *pageToRemove);
void addPageToList(struct page_link *pageToAdd);
int moveToDisk(struct page_link *toSwap);
int movePageToFile(struct page_link *pageToWrite);
int getPageFromFile(struct page_link *page_link);
int nextAvOffset(void);
void deletePage(struct page_link *pageToRemove);
void setFlagsOnSwap(pde_t *pgdir, char *virtAddr, int swapIn);
int swapPages(uint addr);
struct page_link* choosePageToSwap(void);
uint numOfOnes(uint shiftCounter);
uint is_PTE_A(pde_t *pgdir, char *virtualAddr);
void PTE_A_off(pde_t *pgdir, char *virtualAddr);
void switchQueueAQ(pde_t *pgdir);

void print_list(void);

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

int mappages_aux(pde_t *pgdir, void *va, uint size, uint pa, int perm){
  return mappages(pgdir, va, size, pa, perm);
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    //task 1: if SELECTION is not NONE and process is not init or shell, use page framework
    if ((SELECTION != NONE) && (myproc()->pid > 2)){
      if (myproc()->num_pages_ram == MAX_PSYC_PAGES){
         // Check if max page number has been reached
        if (myproc()->num_pages_disk + MAX_PSYC_PAGES == MAX_TOTAL_PAGES){
              cprintf("allocuvm: max number of pages reached, cannot allocate more pages for process\n");
              return 0;
        }
        if (myproc()->swapFile == 0){
            createSwapFile(myproc());
        }
        if (moveToDisk(choosePageToSwap()) == -1){ //if num of pages = max total pages, choose page from ram, and move it to file
            return 0;
        }
      }
    }
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
    if ((SELECTION != NONE) && (myproc()->pid>2)){
      addPageToRam((char *)a);  // Add the new page to Ram
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;
  int pageFound;
  struct page_link *page_link;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    //page is in RAM, so we delete the page and swap in a page from the swap file if one exists.
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree (deallocuvm)");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
      if ((SELECTION != NONE) && (myproc()->pgdir == pgdir)){ //if paging in on
        for (page_link = &myproc()->pages_meta_data[0]; page_link < &myproc()->pages_meta_data[MAX_TOTAL_PAGES]; page_link++){
          if (page_link->page.page_id == a){
            pageFound = 1;
            break;
          }   
        }
        if (pageFound){
          deletePage(page_link);
        }
      }
    }
    else if((SELECTION != NONE) && (*pte & PTE_PG) && (myproc()->pgdir == pgdir)){ // if paging is on and page is in the swap file
      uint page_id = PTE_ADDR((uint) a);
      for (page_link = &myproc()->pages_meta_data[0]; page_link < &myproc()->pages_meta_data[MAX_TOTAL_PAGES]; page_link++){
        if (page_link->page.page_id == page_id){
          pageFound = 1;
          break;
        } 
      }
      if (pageFound){ // if page is found in the pages_meta_data, clear the struct values
        myproc()->num_pages_disk--;
        myproc()->available_Offsets[page_link->page.offset_in_file / PGSIZE] = 0; // free the offset in the offsets array
        page_link->page.page_id = 0xFFFFFFFF;           
        page_link->page.offset_in_file = -1;                
        page_link->page.state = NOT_USED;
      }
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;
  if(pgdir == 0)
    panic("freevm: no pgdir");
  global_pgdir_flag = 1;
  global_pgdir = pgdir;
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  global_pgdir_flag = 0;
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz) //COW
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!((*pte & PTE_P) || (*pte & PTE_PG))) // page can be also in swap file
      panic("copyuvm COW: page not present in RAM or swap file");

    if(*pte & PTE_P){
      pa = PTE_ADDR(*pte);
      *pte = *pte & ~PTE_W;
      flags = PTE_FLAGS(*pte);
      if(mappages(d, (void*)i, PGSIZE, pa, flags) < 0) {
        goto bad;
      }
      incrementRef(P2V(pa));
    }
    else{
      pte_t *pte2 = walkpgdir(d, (void *) i, 1);
      *pte2 = *pte & 0xfff;
    }
  }
  lcr3(V2P(pgdir));

  return d;

bad:
  freevm(d);
  return 0;
}


pde_t*
copyuvm_no_COW(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!((*pte & PTE_P) || (*pte & PTE_PG))) // page can be also in swap file
      panic("copyuvm: page not present in RAM or swap file");

    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    
    if(*pte & PTE_P){ // page is in RAM
      if((mem = kalloc()) == 0)
        goto bad;
      memmove(mem, (char*)P2V(pa), PGSIZE);
      if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
        kfree(mem);
        goto bad;
      }
    }
    else{ // page is in swapfile
      pte_t *page_in_file_pte = walkpgdir(d, (void*) i, 1); // find the pte of the page that is in the swap file, create the pde if needed
      *page_in_file_pte = (*pte & 0xfff);
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//*************************** added functions: Task 1 ***************************

//add a new page to the RAM list (also allocates space in the page_link array)
void addPageToRam(char * addr){
  struct page_link *lastPage = myproc()->page_list_head_ram;
  struct page_link *newPageLink = 0;
  int index = 0;
  int pageFound = 0;
  // if page_list_head_ram is null then set it's virtual address to pages_meta_data[0]
  if (lastPage == 0){
      newPageLink = &(myproc()->pages_meta_data[0]);
      myproc()->page_list_head_ram = newPageLink;
  }
  // Search for last link in Ram pages list
  else{
    while(lastPage->next != 0){
      lastPage = lastPage->next;
    }
    // Search if already exists entry in page directory table/ total pages array
    for(newPageLink = &(myproc()->pages_meta_data[0]); newPageLink < &(myproc()->pages_meta_data[MAX_TOTAL_PAGES]); newPageLink++){
      if (newPageLink->page.page_id == PTE_ADDR((uint) addr)){
        pageFound = 1;
        break;
      } 
      index++;
    } 
    // If entry doesn't exist, add a new page to array
    if (!pageFound){
      index = 0;
      //searching for a place in the array that is unused
      for(newPageLink = &(myproc()->pages_meta_data[0]); newPageLink < &(myproc()->pages_meta_data[MAX_TOTAL_PAGES]); newPageLink++){
        if(newPageLink->page.state == NOT_USED) 
        {
          break;
        }   
        index++;
      } 
    }
    //index now contains the place in the array that should contain the new page
    lastPage->next = newPageLink;
    newPageLink->prev = lastPage; 
  }
  // Initialize page struct fields
  newPageLink->next = 0;
  newPageLink->page.page_id = PTE_ADDR(addr);
  newPageLink->page.index = index;
  newPageLink->page.state = IN_MEMORY;
  newPageLink->page.offset_in_file = -1;
  newPageLink->page.shiftCounter = (SELECTION == LAPA) ? 0xFFFFFFFF : 0;
  // Update array of pages
  myproc()->num_pages_ram++;   
}

// moves a page from RAM to the swap file
int moveToDisk(struct page_link *toSwap){
  // Check if should use the system global pgdir
  pde_t * pgdir = (global_pgdir_flag == 1) ? global_pgdir : myproc()->pgdir;

  pte_t *pte = walkpgdir(pgdir, (char*)toSwap->page.page_id, 0);
   // Write page to swap file from page's virtual address
  if (movePageToFile(toSwap) == -1){
    return -1;
  }
  myproc()->num_of_page_outs++;

  // Update list pointers
  removePageFromList(toSwap);

  setFlagsOnSwap(pgdir, (char *)toSwap->page.page_id, 0); // set relevant flags

  // free memory from Ram
  uint page_id = PTE_ADDR(*pte);
  char * v_address = P2V(page_id);
  kfree(v_address);
  lcr3(V2P(pgdir));
  myproc()->num_pages_ram--;
  return 0;
}

void
removePageFromList(struct page_link *pageToRemove){
  struct page_link *previousPage;
  struct page_link *nextPage;
  // Check if not head of list
  if(pageToRemove->prev == 0){
    // Set process pointer to head of list
    myproc()->page_list_head_ram = pageToRemove->next;
    if (pageToRemove->next != 0){ //if it wasn't a single link in the list then update next to be the new head
      nextPage = pageToRemove->next;
      nextPage->prev = 0;
    }
  }
  else{
    previousPage = pageToRemove->prev;
    previousPage->next = pageToRemove->next;
    // if next is not null, updated next->prev = toremove->prev;
    if (previousPage->next != 0){
      nextPage = previousPage->next;
      nextPage->prev = previousPage;
    }
  }
  // Set pointer of current page
  pageToRemove->prev = 0;
  pageToRemove->next = 0;
}

// add page to the RAM pages list to the end of the list 
void addPageToList(struct page_link *pageToAdd){
  if(myproc()->page_list_head_ram == 0){
    myproc()->page_list_head_ram = pageToAdd;
    pageToAdd->next = 0;
    pageToAdd->prev = 0;
  }
  struct page_link *lastPage = myproc()->page_list_head_ram;
  while (lastPage->next != 0){
      lastPage = lastPage->next;
  }
  lastPage->next = pageToAdd;
  pageToAdd->prev = lastPage;
  pageToAdd->next = 0;
}

//move a page from RAM to the swap file (aux for moveToDisk() )
int movePageToFile(struct page_link *pageToWrite){
  // Check if should use the system global pgdir
  pde_t *pgdir = (global_pgdir_flag == 1) ? global_pgdir : myproc()->pgdir;
  pte_t *pte = walkpgdir(pgdir, (char*)pageToWrite->page.page_id, 0);

  if (pte == 0){
    panic ("movePageToFile: page doesn't exist\n");   
  }
  if (!(*pte & PTE_P)){
    cprintf("pageToWrite: %x 0x%x %d ",pageToWrite->page.page_id, *pte,pageToWrite->page.state);
    panic ("movePageToFile: page isn't present(2)\n"); 
  }
  uint offset = nextAvOffset(); //get a free offset index in the swap file
  if(offset == -1)
    panic("movePageToFile: swapFile is full!"); 
  if (writeToSwapFile(myproc(), (char*) pageToWrite->page.page_id, PGSIZE*offset, PGSIZE) == -1){
    cprintf("ERROR in movePageToFile\n");
    return -1;
  }
  pageToWrite->page.offset_in_file = PGSIZE*offset;
  myproc()->available_Offsets[offset] = 1;
  pageToWrite->page.state = IN_DISK;
  myproc()->num_pages_disk++;
  return 0;
}

//get an available offset index in the swap file
int nextAvOffset(){
  for (int i = 0; i < 17; i++){
    if(myproc()->available_Offsets[i] == 0){
      return i;
    }
  }
  return -1;
}

// delete a page from RAM and bring a page from file if it exists
void deletePage(struct page_link *pageToRemove){
  if (myproc()->num_pages_ram > 0){
    myproc()->num_pages_ram--;
  }
  // reset the page entry
  pageToRemove->page.page_id = 0xFFFFFFFF;
  pageToRemove->page.state = NOT_USED;
  pageToRemove->page.offset_in_file = -1;
  //if there is a page is the swap file bring it to RAM
  if (myproc()->num_pages_disk > 0){
    struct page_link *page_link;
    for (page_link = &myproc()->pages_meta_data[0]; page_link < &myproc()->pages_meta_data[MAX_TOTAL_PAGES]; page_link++){
      if (page_link->page.state == IN_DISK){
        if(getPageFromFile(page_link) == -1){
          return;
        }
        break;
      }   
    } 
  }
}

// get a page from the swap file to RAM
int getPageFromFile(struct page_link *page_link){ 
  if (page_link->page.offset_in_file < 0){
    cprintf("getPageFromFile: page doesn't have an offset in the swap file\n");
    return -1;
  }
  // Check if should use the system global pgdir
  pde_t *pgdir = (global_pgdir_flag == 1) ? global_pgdir : myproc()->pgdir;
  char *addr = kalloc();
  if(addr == 0){
    panic("getPageFromFile: kalloc out of memory\n");
  }

  mappages(pgdir, (char *) page_link->page.page_id, PGSIZE, V2P(addr), PTE_W | PTE_U);

  if (readFromSwapFile(myproc(), addr, page_link->page.offset_in_file, PGSIZE) == -1){
    cprintf("getPageFromFile: read from swap file failed\n");
    return -1;
  }
  
  setFlagsOnSwap(pgdir, (char *) page_link->page.page_id, 1); // set relevant flags
  myproc()->available_Offsets[page_link->page.offset_in_file/PGSIZE] = 0;
  myproc()->num_pages_ram++;
  myproc()->num_pages_disk--; 
  page_link->page.offset_in_file = -1;
  page_link->page.state = IN_MEMORY;

  // add page_link the the RAM list
  addPageToList(page_link);

  return 0;
}

// if swapIn == 1 then set present flag and clear PG flag, otherwise clear present flag and set PG flag
void
setFlagsOnSwap(pde_t *pgdir, char *virtAddr, int swapIn){ //set present flag and clear page_out flag
  pte_t *pte = walkpgdir(pgdir, virtAddr, 0);
  if(pte == 0){
    panic("setFlagsOnSwap: pte == 0\n");
  }
  if(swapIn){
    *pte = *pte & ~PTE_PG;  // Clear PTE_PG flag
    *pte = *pte | PTE_P;    // Set PTE_P flag
  }
  else{
    *pte = *pte & ~PTE_P;   // Clear PTE_P flag
    *pte = *pte | PTE_PG;   // Set PTE_PG flag
  }
}

//aux function for other files to use walkpgdir function non static
pte_t* walkpgdir_aux(pde_t *pgdir, const void *va, int alloc){
  return walkpgdir(pgdir, va, alloc);
}

int
swapPages(uint addr){
  struct page_link *page_link_out = choosePageToSwap();  // Choose a page to swap out
  struct page_link *page_link_in;
  int flag = 0;
  // Look for the required page entry in pages meta data array
  for (page_link_in = &myproc()->pages_meta_data[0]; page_link_in < &myproc()->pages_meta_data[MAX_TOTAL_PAGES]; page_link_in++){
    if (page_link_in->page.page_id == addr){
      flag = 1;
      break;
    }   
  } 
  // didn't find the page
  if (!flag){
    return -1;
  }

  if(page_link_out != 0){
    // Write page to swap file and read from swap file
    moveToDisk(page_link_out);
  }
  getPageFromFile(page_link_in);
  return 0;
}

// chooses a page to swap out of RAM according to the current selection of page replacement schemes
struct page_link* choosePageToSwap(){
  struct page_link *page_link = myproc()->page_list_head_ram;
  struct page_link *tmp;
  pte_t *pte;
  int found = 0;
  // Check if should use the system global pgdir
  pde_t *pgdir = (global_pgdir_flag == 1) ? global_pgdir : myproc()->pgdir;

  // If list is null (no page to swap out) --> return to calling function allocuvm
  if (!page_link){
    return 0;
  }

  // at the end of the Switch-Case, page_link holds the page to swap out
  switch(SELECTION){
    case(LIFO):
      while(page_link->next != 0){
        page_link = page_link->next;
      }
      break;
    case(NFUA):
      tmp = myproc()->page_list_head_ram;
      while(tmp->next != 0){
        tmp = tmp->next;
        int shiftCounter = tmp->page.shiftCounter;
        pte = walkpgdir(myproc()->pgdir,(void*)(tmp->page.page_id),0);
        if(!(*pte & PTE_U)){ // ignore non user pages
          continue;
        }
        if(shiftCounter < page_link->page.shiftCounter){
          page_link = tmp;
        }
      }
      break;
    case(LAPA): //Least accessed page + AGING
      // Find least accessed page to swap out
      tmp = myproc()->page_list_head_ram;
      int shiftCounter2 = tmp->page.shiftCounter;
      int min_number_of_ones = numOfOnes(shiftCounter2); 
      while(tmp->next != 0){
        tmp = tmp->next;
        shiftCounter2 = tmp->page.shiftCounter;
        int current_numOfOnes = numOfOnes(shiftCounter2);
        pte = walkpgdir(myproc()->pgdir,(void*)tmp->page.page_id,0);
        if(!(*pte & PTE_U)){ // ignore non user pages
          continue;
        }
        if (current_numOfOnes < min_number_of_ones){
          page_link = tmp;
          min_number_of_ones = current_numOfOnes;
        }
        else if((current_numOfOnes == min_number_of_ones) && (shiftCounter2 < page_link->page.shiftCounter)){
          page_link = tmp;
          min_number_of_ones = current_numOfOnes;
        }
      }
      break;
    case(SCFIFO):   //Second Chance First In First Out
      while (!found){
        pte = walkpgdir(myproc()->pgdir,(void*)page_link->page.page_id,0);
        if(!(*pte & PTE_U)){ // ignore non user pages
          removePageFromList(page_link);
          addPageToList(page_link); //add the page to the end of the list
          page_link = myproc()->page_list_head_ram;
          continue;
        }
        // if PTE_A bit is set remove page and add to end of list
        if (is_PTE_A(pgdir, (char *)page_link->page.page_id) > 0){
           PTE_A_off(pgdir, (char *)page_link->page.page_id);
           removePageFromList(page_link);
           addPageToList(page_link); //add the page to the end of the list
           page_link = myproc()->page_list_head_ram;
         }
        else{
           found = 1;
         }
      }
      break;
    case(AQ): //Advancing Queue
      page_link = myproc()->page_list_head_ram; // the first page in the list is the first to leave the Advancing Queue
      break;  
  }
  return page_link;
}

uint
is_PTE_A(pde_t *pgdir, char *virtualAddr){
  pte_t *pte;
  pte = walkpgdir(pgdir, virtualAddr, 0);
  if(pte == 0)
    panic("is_PTE_A: fail");
  int phAdress = 0;
  phAdress = *pte;
  phAdress = phAdress & PTE_A;   
  return (phAdress != 0) ? 1 : 0; // 1 if the bit is on , else 0  
}

void
PTE_A_off(pde_t *pgdir, char *virtualAddr){
  pte_t *pte;
  pte = walkpgdir(pgdir, virtualAddr, 0);
  if(pte == 0)
    panic("PTE_A_off: fail");
  *pte = *pte & (~PTE_A);
}

uint
numOfOnes(uint shiftCounter){
  int i = 0, counter = 0;
  while(i < 32){
    if((shiftCounter & 1) > 0 ){
      counter ++;
    }
    shiftCounter >>= 1;
    i++;
  }
  return counter;
}

//update the AQ: for each page that it's PTE_A bit is set, swap it with it's preceeding page in the queue
void
switchQueueAQ(pde_t *pgdir){
  struct page_link *page_link = myproc()->page_list_head_ram;
  struct page_link *nextLink;
  while(page_link != 0 && page_link->next != 0){
    if(is_PTE_A(pgdir, (char*)page_link->page.page_id) > 0){
      PTE_A_off(pgdir, (char*)page_link->page.page_id);
      nextLink = page_link->next;
      if(is_PTE_A(pgdir, (char*)nextLink->page.page_id) == 0){ //swap
        struct page_link *tmp = page_link->prev;
        struct page_link *tmp2 = nextLink->next;
        if(page_link->prev == 0) // current node
          myproc()->page_list_head_ram = nextLink;
        page_link->prev = nextLink;
        page_link->next = nextLink->next;
        nextLink->prev = tmp;
        nextLink->next = page_link;
        if(tmp!=0)
          tmp->next = nextLink;
        if(tmp2!=0)
          tmp2->prev = page_link;
      }
    }
    else{
    page_link = page_link->next;
    }
  }
  if(page_link != 0){
    PTE_A_off(pgdir, (char*)page_link->page.page_id);
  }
}

//**************************************************************************

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

