#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

//extern pte_t* walkpgdir(pde_t *pgdir, const void *va, int create);
//extern int mappages(pde_t *pgdir, void *la, uint size, uint pa, int perm);

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  
  initlock(&tickslock, "time");
}

static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int create)
{
  uint r;
  pde_t *pde;
  pte_t *pgtab;
 
  pde = &pgdir[PDX(va)];
  //*pde = *pde & ~PTE_W;
  if(*pde & PTE_P)
  {
     pgtab = (pte_t*) PTE_ADDR(*pde);
  }
  else if(!create || !(r = (uint) kalloc()))
     return 0;
  else
  {
		pgtab = (pte_t*) r;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table 
    // entries, if necessary.
    *pde = PADDR(r) | PTE_P | PTE_W | PTE_U;
    //*pde = PADDR(r) | PTE_P | PTE_U;
  }
  return &pgtab[PTX(va)];
}

static int
mappages(pde_t *pgdir, void *la, uint size, uint pa, int perm)
{
  char *a = PGROUNDDOWN(la);
  char *last = PGROUNDDOWN(la + size - 1);
 
  while(1)
	{
	  pte_t *pte = walkpgdir(pgdir, a, 1);
    if(pte == 0)
			return 0;
		//if(*pte & PTE_P)
		//  panic("remap");
		*pte = pa | perm | PTE_P;
		if(a == last)
		  break;
		a += PGSIZE;
	  pa += PGSIZE;
	}
	return 1;

}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL)
	{
    if(proc->killed)
      exit();
    proc->tf = tf;
    syscall();
    if(proc->killed)
      exit();
    return;
  }

  switch(tf->trapno)
	{
  case T_IRQ0 + IRQ_TIMER:
    if(cpu->id == 0){
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
            cpu->id, tf->cs, tf->eip);
    lapiceoi();
    break;
	
	case T_PGFLT:
		cprintf("cr2: %d\n", rcr2());
		if (!((uint)rcr2() & PTE_W))
		{
			cprintf("procid: %d\n", proc->pid);
			cprintf("page fault due to not writeable.\n");

			pte_t *pte;
			char *mem;
			uint pa;
	
			if(!(pte = walkpgdir(proc->pgdir, (void *)rcr2(), 0)))
				panic("pte should exist\n");

			cprintf("PTE: %d\n", pte);
			if(!(*pte & PTE_P))
				panic("page not present\n");
			pa = PTE_ADDR(*pte);
			if(!(mem = kalloc()))
				panic("kalloc fail\n");
			memmove(mem, (char *)pa, PGSIZE);
			if(!mappages(proc->pgdir, (void *)rcr2(), PGSIZE, PADDR(mem), PTE_W | PTE_U))
					panic("mappages fail\n");

		}
		else
		{
				if(proc == 0 || (tf->cs&3) == 0){
					// In kernel, it must be our mistake.
					cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
									tf->trapno, cpu->id, tf->eip, rcr2());
					panic("trap");
				}
				// In user space, assume process misbehaved.
				cprintf("pid %d %s: trap %d err %d on cpu %d "
								"eip 0x%x addr 0x%x--kill proc\n",
								proc->pid, proc->name, tf->trapno, tf->err, cpu->id, tf->eip, 
								rcr2());
				proc->killed = 1;
		}
		break;

  default:
    if(proc == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpu->id, tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            proc->pid, proc->name, tf->trapno, tf->err, cpu->id, tf->eip, 
            rcr2());
    proc->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running 
  // until it gets to the regular system call return.)
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(proc && proc->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();
}
