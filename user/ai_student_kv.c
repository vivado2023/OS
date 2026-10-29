#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "ai_student_impl.h"

enum {
  KV_PHASE = 1,
  KV_FD,
  KV_NEXT,
};

enum {
  KV_NONE,
  KV_READY,
  KV_WRITING,
  KV_WRITTEN,
  KV_READING,
  KV_FAILED,
};

int
ai_student_kv_begin(struct ai_session *session)
{
  if(session == 0 || session->state[KV_PHASE] != KV_NONE ||
     session->worker < 0 || session->worker >= AI_MAX_WORKERS ||
     session->requests <= 0 || session->requests > AI_MAX_REQUESTS)
    return -1;

  session->state[KV_FD] = -1;
  session->state[KV_NEXT] = 0;
  session->state[KV_PHASE] = KV_READY;
  return 0;
}

int
ai_student_kv_begin_write(struct ai_session *session)
{
  char name[AI_STREAM_NAME_LEN];
  int fd;

  if(session == 0 || session->state[KV_PHASE] != KV_READY)
    return -1;

  ai_student_kv_name(name, session->worker);

  unlink(name);
  session->state[KV_PHASE] = KV_FAILED;
  fd = open(name, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    return -1;
  session->state[KV_FD] = fd;
  session->state[KV_PHASE] = KV_WRITING;
  return 0;
}

int
ai_student_kv_store(struct ai_session *session, int request,
                    struct kv_entry *kv, struct ai_io *io)
{
  int offset, n;

  if(session == 0 || session->state[KV_PHASE] != KV_WRITING ||
     kv == 0 || io == 0 || request < 0 || request >= session->requests ||
     request != session->state[KV_NEXT])
    return -1;

  for(offset = 0; offset < AI_KV_FILE_BYTES; offset += n) {
    n = write(session->state[KV_FD], (char *)kv + offset,
              AI_KV_FILE_BYTES - offset);
    if(n <= 0) {
      session->state[KV_PHASE] = KV_FAILED;
      return -1;
    }
  }
  io->kv_write_bytes += AI_KV_FILE_BYTES;
  session->state[KV_NEXT]++;
  return 0;
}

int
ai_student_kv_end_write(struct ai_session *session)
{
  int result;

  if(session == 0 || session->state[KV_PHASE] != KV_WRITING ||
     session->state[KV_NEXT] != session->requests)
    return -1;

  result = close(session->state[KV_FD]);
  session->state[KV_FD] = -1;
  session->state[KV_PHASE] = result < 0 ? KV_FAILED : KV_WRITTEN;
  return result;
}

int
ai_student_kv_begin_read(struct ai_session *session)
{
  char name[AI_STREAM_NAME_LEN];
  int fd;

  if(session == 0 || session->state[KV_PHASE] != KV_WRITTEN)
    return -1;

  ai_student_kv_name(name, session->worker);
  fd = open(name, O_RDONLY);
  if(fd < 0) {
    session->state[KV_PHASE] = KV_FAILED;
    return -1;
  }
  session->state[KV_FD] = fd;
  session->state[KV_NEXT] = 0;
  session->state[KV_PHASE] = KV_READING;
  return 0;
}

int
ai_student_kv_restore(struct ai_session *session, int request,
                      struct kv_entry *kv, struct ai_io *io)
{
  int offset, n;

  if(session == 0 || session->state[KV_PHASE] != KV_READING ||
     kv == 0 || io == 0 || request < 0 || request >= session->requests ||
     request != session->state[KV_NEXT])
    return -1;

  for(offset = 0; offset < AI_KV_FILE_BYTES; offset += n) {
    n = read(session->state[KV_FD], (char *)kv + offset,
             AI_KV_FILE_BYTES - offset);
    if(n <= 0) {
      session->state[KV_PHASE] = KV_FAILED;
      return -1;
    }
  }
  io->kv_read_bytes += AI_KV_FILE_BYTES;
  session->state[KV_NEXT]++;
  return 0;
}

void
ai_student_kv_end(struct ai_session *session)
{
  char name[AI_STREAM_NAME_LEN];

  if(session == 0 || session->state[KV_PHASE] == KV_NONE)
    return;

  if(session->state[KV_FD] >= 0)
    close(session->state[KV_FD]);
  if(session->state[KV_PHASE] != KV_READY) {
    ai_student_kv_name(name, session->worker);
    unlink(name);
  }
  session->state[KV_FD] = -1;
  session->state[KV_NEXT] = 0;
  session->state[KV_PHASE] = KV_NONE;
}
