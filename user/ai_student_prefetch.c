#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "ai_student_impl.h"

enum {
  PREFETCH_EMPTY,
  PREFETCH_PENDING,
  PREFETCH_READY,
  PREFETCH_FAILED,
};

struct prefetch_window {
  struct ai_session *owner;
  int pid;
  int request_fd;
  int data_fd;
  int phase;
  int request;
  int next;
  char data[AI_KV_FILE_BYTES];
};

static struct prefetch_window prefetch;

static int
read_exact(int fd, void *buffer, int length)
{
  char *p = buffer;
  int n;

  while(length > 0) {
    n = read(fd, p, length);
    if(n <= 0)
      return -1;
    p += n;
    length -= n;
  }
  return 0;
}

static int
write_exact(int fd, void *buffer, int length)
{
  char *p = buffer;
  int n;

  while(length > 0) {
    n = write(fd, p, length);
    if(n <= 0)
      return -1;
    p += n;
    length -= n;
  }
  return 0;
}

static struct prefetch_window *
find_window(struct ai_session *session)
{
  if(session == 0 || session->worker < 0 ||
     session->worker >= AI_MAX_WORKERS)
    return 0;
  return prefetch.owner == session ? &prefetch : 0;
}

static int
prefetch_fail(struct prefetch_window *window)
{
  if(window)
    window->phase = PREFETCH_FAILED;
  return -1;
}

static void
helper_loop(struct ai_session *session, int request_fd, int data_fd,
            char *scratch)
{
  char name[AI_STREAM_NAME_LEN];
  int fd, request, expected = 0;
  int status = 1;

  for(fd = 0; fd < NOFILE; fd++)
    if(fd != request_fd && fd != data_fd)
      close(fd);

  ai_student_kv_name(name, session->worker);
  fd = open(name, O_RDONLY);
  if(fd >= 0) {
    while(read_exact(request_fd, &request, sizeof(request)) == 0) {
      if(request == -1) {
        status = 0;
        break;
      }
      if(request != expected || request < 0 || request >= session->requests)
        break;
      if(read_exact(fd, scratch, AI_KV_FILE_BYTES) < 0 ||
         write_exact(data_fd, &request, sizeof(request)) < 0 ||
         write_exact(data_fd, scratch, AI_KV_FILE_BYTES) < 0)
        break;
      expected++;
    }
    close(fd);
  }
  close(request_fd);
  close(data_fd);
  exit(status);
}

int
ai_student_prefetch_begin(struct ai_session *session)
{
  struct prefetch_window *window;
  int requests[2], data[2], pid;

  if(session == 0 || session->worker < 0 ||
     session->worker >= AI_MAX_WORKERS || session->requests <= 0 ||
     session->requests > AI_MAX_REQUESTS)
    return -1;
  window = &prefetch;
  if(window->owner)
    return -1;

  if(pipe(requests) < 0)
    return -1;
  if(pipe(data) < 0) {
    close(requests[0]);
    close(requests[1]);
    return -1;
  }
  pid = fork();
  if(pid < 0) {
    close(requests[0]);
    close(requests[1]);
    close(data[0]);
    close(data[1]);
    return -1;
  }
  if(pid == 0)
    helper_loop(session, requests[0], data[1], window->data);

  close(requests[0]);
  close(data[1]);
  window->owner = session;
  window->pid = pid;
  window->request_fd = requests[1];
  window->data_fd = data[0];
  window->phase = PREFETCH_EMPTY;
  window->request = -1;
  window->next = 0;
  return 0;
}

int
ai_student_prefetch_next(struct ai_session *session, int request)
{
  struct prefetch_window *window = find_window(session);

  if(window == 0 || window->phase != PREFETCH_EMPTY || request < 0 ||
     request >= session->requests || request != window->next)
    return prefetch_fail(window);

  if(write_exact(window->request_fd, &request, sizeof(request)) < 0)
    return prefetch_fail(window);
  window->request = request;
  window->phase = PREFETCH_PENDING;
  return 0;
}

int
ai_student_prefetch_wait(struct ai_session *session, int request)
{
  struct prefetch_window *window = find_window(session);
  int reply;

  if(window == 0 || window->phase != PREFETCH_PENDING ||
     request != window->request)
    return prefetch_fail(window);

  if(read_exact(window->data_fd, &reply, sizeof(reply)) < 0 || reply != request ||
     read_exact(window->data_fd, window->data, AI_KV_FILE_BYTES) < 0)
    return prefetch_fail(window);
  window->phase = PREFETCH_READY;
  return 0;
}

int
ai_student_prefetch_restore(struct ai_session *session, int request,
                            struct kv_entry *kv, struct ai_io *io)
{
  struct prefetch_window *window = find_window(session);

  if(window == 0 || window->phase != PREFETCH_READY ||
     request != window->request || kv == 0 || io == 0)
    return prefetch_fail(window);

  memmove(kv, window->data, AI_KV_FILE_BYTES);
  io->kv_read_bytes += AI_KV_FILE_BYTES;
  window->next++;
  window->request = -1;
  window->phase = PREFETCH_EMPTY;
  return 0;
}

void
ai_student_prefetch_end(struct ai_session *session)
{
  struct prefetch_window *window = find_window(session);
  int stop = -1;
  int child;

  if(window == 0)
    return;

  close(window->data_fd);
  write_exact(window->request_fd, &stop, sizeof(stop));
  close(window->request_fd);

  while((child = wait(0)) >= 0 && child != window->pid)
    ;
  memset(window, 0, sizeof(*window));
}
