#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

int swapPages(uint addr);
void switchQueueAQ(pde_t *pgdir);

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

void update_meta_data(void){
  struct proc *currproc = myproc();
  if((SELECTION == LAPA) || (SELECTION == NFUA)){
    struct page_link *tmp = currproc->page_list_head_ram;
    while(tmp != 0){
      tmp->page.shiftCounter >>= 1;
      if(is_PTE_A(currproc->pgdir, (char*)tmp->page.page_id)){
        PTE_A_off(currproc->pgdir, (char*)tmp->page.page_id);
        tmp->page.shiftCounter |= 0x80000000;
      }
      tmp = tmp->next;
    }
  }
  if (SELECTION == AQ){
    switchQueueAQ(myproc()->pgdir); //update the AQ
  }
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT:
    myproc()->num_of_page_faults++;
    uint pg_fault_addr = rcr2();  // get the address that caused the page fault from rcr2 register
    pte_t *pte = walkpgdir_aux(myproc()->pgdir, (char*)pg_fault_addr, 0);
    //cprintf("REACHED PGFLT! Process %d %s Looking for entry: 0x%x pte: %x\n", myproc()->pid, myproc()->name, pg_fault_addr,pte);
    
    // If SELECTION is NONE *OR* current proc is shell or init, handle page fault as default xv6
    if(!(*pte & PTE_P) && ((SELECTION == NONE) || (myproc()->pid <= 2))){  
      cprintf("T_PGFLT: SELECTION=NONE / init / shell");
      goto pg_fault;
    }

    if(*pte & PTE_P){ //if true then it is a write to a read-only page
      void *pa = (void*)PTE_ADDR(*pte);
      if(getRef(P2V(pa)) == 1){ // when there is only one reference we just need to grant write permission
        *pte = *pte | PTE_W;
      }
      else if(getRef((char*)P2V(pa)) > 1){ // when more then 1 reference for the same physical page we need to copy the page
        char *mem = kalloc();
        kmemLock();
        uint flags = PTE_FLAGS(*pte);
        flags = flags | PTE_W;
        if(mem == 0){
          myproc()->killed = 1;
        }
        else{ // make a deep copy of this page and grant the current proc write permission
          memmove(mem, (void*)PGROUNDDOWN(pg_fault_addr), PGSIZE);
          //mappages_aux(myproc()->pgdir, (void*)PGROUNDDOWN(pg_fault_addr), PGSIZE, V2P(mem), flags);
          *pte = V2P(mem) | flags;
          lcr3(V2P(myproc()->pgdir));
        }
        kmemRelease();
        decrementRef((char*)P2V(pa));
      }
      else{ //ref==0 
        goto pg_fault;      // segmentation fault: page was not found in Ram or DISK (in COW probably illegal address)
      }
    }
    else if ((*pte & PTE_PG) == 0){
      goto pg_fault;      // segmentation fault: page was not found in Ram or DISK
    }
    else{
      update_meta_data();
      // Swap pages - get the required page from the swap file and write a chosen page by policy from physical memory to the swap file instead
      if (swapPages(PGROUNDDOWN(pg_fault_addr)) == -1){
        // swapPages didn't find the page in the swap file --> ERROR, shouldn't reach here because we check PTE_PG bit
        cprintf("T_PGFLT error: 'swap-pages' func didnt find the address: 0x%x in the swap file\n",pg_fault_addr);
        goto pg_fault;
      }
    }
    break;

  //PAGEBREAK: 13
  default:
    pg_fault:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
