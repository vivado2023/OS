b _entry
  c
  delete

  b start
  c
  delete

  b kernel/start.c:58
  c
  p/x &main
  p/x $mepc
  delete

  b main
  c
  delete

  b userinit
  c
  delete

  b scheduler
  c
  delete

  b swtch
  c
  x/i $pc
  si
  delete

  b forkret
  c
  p cpus[$tp]->proc->name
  delete

  b exec
  c
  p cpus[$tp]->proc->name
  delete

  finish
  p cpus[$tp]->proc->name
