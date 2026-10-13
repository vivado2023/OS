#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

extern struct proc proc[NPROC];

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_yield(void) {
  struct proc *p = myproc();
  int index = p - proc;

  printf("Save the context of the process to the memory region from address %p to %p\n",
         (uint64)&p->context, (uint64)(&p->context + 1));
  printf("Current running process pid is %d and user pc is %p\n", p->pid, p->trapframe->epc);

  // Search from the process after p, wrapping around the process table.
  for (int offset = 1; offset < NPROC; offset++) {
    struct proc *candidate = &proc[(index + offset) % NPROC];
    acquire(&candidate->lock);
    if (candidate->state == RUNNABLE) {
      printf("Next runnable process pid is %d and user pc is %p\n", candidate->pid,
             candidate->trapframe->epc);
      release(&candidate->lock);
      break;
    }
    release(&candidate->lock);
  }

  yield();
  return 0;
}

uint64 sys_seccomp_ctl(void) {
  int op;
  uint64 arg;
  struct proc *p = myproc();

  if (argint(0, &op) < 0 || argaddr(1, &arg) < 0)
    return -1;

  if (op == 0) {
    p->seccomp_mask = arg;
    return 0;
  }
  if (op == 1) {
    if (arg > NPROC)
      return -1;
    p->max_children = arg;
    return 0;
  }
  return -1;
}

uint64 sys_seccomp_getlog(void) {
  uint64 buf_addr, len_addr;
  int capacity, count;
  struct proc *p = myproc();

  if (argaddr(0, &buf_addr) < 0 || argaddr(1, &len_addr) < 0)
    return -1;
  if (copyin(p->pagetable, (char *)&capacity, len_addr, sizeof(capacity)) < 0 || capacity < 0)
    return -1;

  count = capacity < p->seccomp_log_len ? capacity : p->seccomp_log_len;
  if (count > 0 &&
      copyout(p->pagetable, buf_addr, (char *)p->seccomp_log, count * sizeof(uint64)) < 0)
    return -1;
  if (copyout(p->pagetable, len_addr, (char *)&count, sizeof(count)) < 0)
    return -1;
  return 0;
}

uint64 sys_wait(void) {
  uint64 p;
  int flags;
  if (argaddr(0, &p) < 0) return -1;
  if (argint(1, &flags) < 0) return -1;
  return wait(p, flags);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}
