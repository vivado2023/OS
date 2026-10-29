#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#include "ai_student_impl.h"

struct model_cache {
  uchar valid[AI_TOTAL_SHARDS];
  char data[AI_TOTAL_SHARDS][AI_SHARD_BYTES];
};

int
ai_student_model_begin(struct ai_session *session)
{
  struct model_cache *cache;

  if(session == 0 || session->memory != 0)
    return -1;
  cache = malloc(sizeof(*cache));
  if(cache == 0)
    return -1;
  memset(cache->valid, 0, sizeof(cache->valid));
  session->memory = cache;
  return 0;
}

int
ai_student_model_load(struct ai_session *session, uint family, uint shard,
                      char *block, struct ai_io *io)
{
  struct model_cache *cache;
  char name[AI_SHARD_NAME_LEN];
  uint index;
  int fd, offset, n;

  if(session == 0 || session->memory == 0 || block == 0 || io == 0)
    return -1;
  if(family == AI_MODEL_FAMILY && shard < AI_MODEL_SHARDS)
    index = shard;
  else if(family == AI_EMBED_FAMILY && shard < AI_EMBED_SHARDS)
    index = AI_MODEL_SHARDS + shard;
  else
    return -1;

  cache = session->memory;
  if(!cache->valid[index]) {
    ai_shard_name(name, family, shard);
    fd = open(name, O_RDONLY);
    if(fd < 0)
      return -1;

    for(offset = 0; offset < AI_SHARD_BYTES; offset += n) {
      n = read(fd, cache->data[index] + offset, AI_SHARD_BYTES - offset);
      if(n <= 0) {
        close(fd);
        return -1;
      }
    }
    close(fd);

    for(offset = 0; offset < AI_SHARD_BYTES; offset++)
      if((uchar)cache->data[index][offset] != ai_byte(family, shard, offset))
        return -1;
    cache->valid[index] = 1;
    if(family == AI_MODEL_FAMILY)
      io->model_read_bytes += AI_SHARD_BYTES;
    else
      io->embed_read_bytes += AI_SHARD_BYTES;
  }

  memmove(block, cache->data[index], AI_SHARD_BYTES);
  return 0;
}

void
ai_student_model_end(struct ai_session *session)
{
  if(session && session->memory) {
    free(session->memory);
    session->memory = 0;
  }
}
