#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->priority = 0; // Default priority number of every newly created process

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  // Put it here to keep track of the last process that does the Context Switch
  struct proc *lastRunProc = 0;

  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    struct proc *highPriorityProc = 0;
    
    // Find a RUNNABLE and the highest priority process in one loop
    for (p = proc; p < &proc[NPROC]; p++) 
    {
      acquire(&p->lock); // acquire new lock every iteration
      if (p->state == RUNNABLE) 
      {
        // Initial value for the Highest Priority Process
        if (highPriorityProc == 0) 
        {
          highPriorityProc = p;
          continue; // continue the iteration so that it doesnt release the current process lock
        }

        // New Higher Priority process found, release the current process and choose new one
        if (p->priority < highPriorityProc->priority)
        {
          release(&highPriorityProc->lock); // New Higher Priority process found, release the current process
          highPriorityProc = p;
          continue; // continue the iteration so that it doesnt release the current process lock
        }

        // For processes with the same priority
        if (p->priority == highPriorityProc->priority)
        {
          // Check if the current high Priority process is the same as the last running process
          if (lastRunProc == highPriorityProc)
          {
            release(&highPriorityProc->lock); 
            highPriorityProc = p; 
            continue; // continue the iteration so that it doesnt release the current process lock
          }
        }

      }
      release(&p->lock); // release the wrong process
    }

    // Change all the necessary member of the process when the process is chosen (RUNNABLE and High Priority)
    if (highPriorityProc != 0) // Need this part if u want to start the program  
    {
      // Switch to chosen process.  It is the process's job
      // to release its lock and then reacquire it
      // before jumping back to us.
      highPriorityProc->state = RUNNING;
      c->proc = highPriorityProc;

      lastRunProc = highPriorityProc;

      swtch(&c->context, &highPriorityProc->context);

      // Process is done running for now.
      // It should have changed its highPriorityProc->state before coming back.
      c->proc = 0;

      // Release the process at the end
      release(&highPriorityProc->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// Manually added here (pstate). State of the processes in the PCB
int
pstate(void)
{
  struct proc *p;
  int counter = 0;

  struct cpu *c;
  int counterCPU = 0;

  // Display the name board
  printf("pid\t name\t state\t\t parent\n");
  printf("----------------------------------------\n");

  // Loop through the process table to print its pid number, name, state and parent's name
  for(p = proc; p < &proc[NPROC]; p++) // Assign p to the array of struct proc and loop til p is at the address of the final proc array
  {
    if (p->state == SLEEPING)
    {
      // if init process, use (init) in parent
      if (strncmp(p->name, "init", 4) == 0)
      {
        printf("%d\t %s\t SLEEPING\t (init)\n", p->pid, p->name);
        counter++;
      }
      else
      {
        // SLEEPING state
        printf("%d\t %s\t SLEEPING\t %s\n", p->pid, p->name, p->parent->name);
        counter++;
      }
    }
    else if (p->state == RUNNING)
    {
      // RUNNING state
      printf("%d\t %s\t RUNNING\t %s\n", p->pid, p->name, p->parent->name);
      counter++;
    }
    else if (p->state == RUNNABLE)
    {
      // RUNNABLE state
      printf("%d\t %s\t RUNNABLE\t %s\n", p->pid, p->name, p->parent->name);
      counter++;
    }
  }

  // Total number of processes
  printf ("Total: %d\n", counter);

  // Loop through the process table to print its pid number, name, state and parent (Dont understand the for loop, just copied from the code above)
  for(c = cpus; c < &cpus[NCPU]; c++) 
  {
    if (c->proc == 0) // https://stackoverflow.com/questions/29336359/how-do-you-check-if-a-struct-is-initialized-in-c (struct outside main() is init as 0)
    {
      printf("cpu%d: idle\n", counterCPU);  
    }
    else
    {
      printf("cpu%d: %s\n", counterCPU, c->proc->name);  
    }
    counterCPU++; 
  }

  // return the number 22 cause syscall.h define it
  return 22;
}

// Manually added here (A4). State of the processes in the PCB
int
ps(void)
{
  struct proc *p;
  int counter = 0;

  // Display the name board
  printf("pid\t name\t state\t\t priority\n");
  printf("------------------------------------------\n");

  // Loop through the process table to print its pid number, name, state and parent's name
  for(p = proc; p < &proc[NPROC]; p++) // Assign p to the array of struct proc and loop til p is at the address of the final proc array
  {
    if (p->state == SLEEPING)
    {
      // SLEEPING state
      printf("%d\t %s\t SLEEPING\t %d\n", p->pid, p->name, p->priority);
      counter++;
    }
    else if (p->state == RUNNING)
    {
      // RUNNING state
      printf("%d\t %s\t RUNNING\t %d\n", p->pid, p->name, p->priority);
      counter++;
    }
    else if (p->state == RUNNABLE)
    {
      // RUNNABLE state
      printf("%d\t %s\t RUNNABLE\t %d\n", p->pid, p->name, p->priority);
      counter++;
    }
  }

  // return the number 23 cause syscall.h define it
  return 23;
}

// Manually added here (A4). 
int 
set (int pid, int priority)
{
	struct proc *p;

	for(p = proc; p < &proc[NPROC]; p++)
  {
	  if(p->pid == pid)
    {
			p->priority = priority;
			break;
		}
	}

  // return the number 24 cause syscall.h define it
	return 24;
}

// Manually added here (A5)
int 
psinfo(uint64 addr) 
{
    struct proc *p;
    struct psinfo pinfo[NPROC];
    int counter = 0;

    // struct cpu *c;
    // int counterCPU = 0;

    // Loop through the process table to print its pid number, name, state and parent's name
    for (p = proc; p < &proc[NPROC]; p++) 
    {
      if (p->state != UNUSED) 
      {
          pinfo[counter].pid = p->pid;
          strncpy(pinfo[counter].name, p->name, sizeof(pinfo[counter].name));
          pinfo[counter].state = p->state;
          // if init process, use (init) in parent
          if (strncmp(p->name, "init", 4) == 0)
          {
            strncpy(pinfo[counter].parent_name, "(init)", sizeof(pinfo[counter].parent_name));
          }
          else
          {
            strncpy(pinfo[counter].parent_name, p->parent->name, sizeof(pinfo[counter].parent_name));
          }
          counter++;
      }
    }

    // // Got error on the CPU part abandoned
    // for(c = cpus; c < &cpus[NCPU]; c++) 
    // {
    //   if (c->proc == 0) // https://stackoverflow.com/questions/29336359/how-do-you-check-if-a-struct-is-initialized-in-c (struct outside main() is init as 0)
    //   { 
    //     strncpy(pinfo[counterCPU].cpu_process_name, "idle", sizeof(pinfo[counterCPU].cpu_process_name));
    //     printf("Im here 1\n");
    //   }
    //   else
    //   {
    //     strncpy(pinfo[counterCPU].cpu_process_name, c->proc->name, sizeof(pinfo[counterCPU].cpu_process_name));
    //     printf("Im here 2\n");
    //   }
    //   printf("Im here 3");
    //   printf("Kernel side cpu%d: %s\n", counterCPU, pinfo[counterCPU].cpu_process_name);
    //   counterCPU++; 
    // }    

    // Copy from kernel to user based on the file.c format
    if (copyout(myproc()->pagetable, addr, (char *)pinfo, sizeof(pinfo)) < 0)
    {
      return -1;
    } 

    return counter;
}