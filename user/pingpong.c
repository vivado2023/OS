#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int parent_to_child[2];
  int child_to_parent[2];
  int parent_pid = getpid();
  int child_pid;
  char message;

  pipe(parent_to_child);
  pipe(child_to_parent);

  child_pid = fork();

  if (child_pid == 0) {
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    read(parent_to_child[0], &message, 1);
    close(parent_to_child[0]);
    printf("%d: received ping from pid %d\n", getpid(), parent_pid);

    write(child_to_parent[1], "p", 1);
    close(child_to_parent[1]);
  } else {
    close(parent_to_child[0]);
    close(child_to_parent[1]);

    write(parent_to_child[1], "p", 1);
    close(parent_to_child[1]);

    read(child_to_parent[0], &message, 1);
    close(child_to_parent[0]);
    printf("%d: received pong from pid %d\n", parent_pid, child_pid);

    wait(0);
  }

  exit(0);
}
