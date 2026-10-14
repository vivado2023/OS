// Buffer cache.
//
// The buffer cache is a hash table of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct bucket {
  struct spinlock lock;
  struct buf *head;
};

struct {
  struct bucket buckets[NBUCKETS];
  struct buf buf[NBUF];
  struct spinlock evict_lock;
  struct buf *unused;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evict_lock, "bcache.evict");
  for(int i = 0; i < NBUCKETS; i++)
    initlock(&bcache.buckets[i].lock, "bcache.bucket");

  // Buffers without a disk identity are kept outside the hash table.
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->next = bcache.unused;
    bcache.unused = b;
  }
}

// Caller holds the bucket lock. A reference prevents eviction after unlock.
static struct buf*
blookup(struct bucket *bucket, uint dev, uint blockno)
{
  for(struct buf *b = bucket->head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      return b;
    }
  }
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct bucket *bucket = &bcache.buckets[blockno % NBUCKETS];

  acquire(&bucket->lock);
  b = blookup(bucket, dev, blockno);
  release(&bucket->lock);
  if(b){
    acquiresleep(&b->lock);
    return b;
  }

  // Never acquire evict_lock while holding a bucket lock.
  acquire(&bcache.evict_lock);
  acquire(&bucket->lock);
  b = blookup(bucket, dev, blockno);
  release(&bucket->lock);
  if(b){
    release(&bcache.evict_lock);
    acquiresleep(&b->lock);
    return b;
  }

  if(bcache.unused){
    b = bcache.unused;
    bcache.unused = b->next;
  } else {
    // Scan from the start; evict_lock keeps identities stable here.
    for(int n = 0; n < NBUF; n++){
      struct buf *candidate = &bcache.buf[n];
      struct bucket *source = &bcache.buckets[candidate->blockno % NBUCKETS];
      acquire(&source->lock);
      if(candidate->refcnt == 0){
        struct buf **link = &source->head;
        while(*link != candidate)
          link = &(*link)->next;
        *link = candidate->next;
        b = candidate;
        release(&source->lock);
        break;
      }
      release(&source->lock);
    }
  }
  if(!b)
    panic("bget: no buffers");

  // b is detached and unreferenced; no other CPU can find or use it.
  // evict_lock prevents another miss from inserting the same disk block.
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  acquire(&bucket->lock);
  b->next = bucket->head;
  bucket->head = b;
  release(&bucket->lock);
  release(&bcache.evict_lock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// The caller's reference keeps the bucket identity stable until decrement.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  struct bucket *bucket = &bcache.buckets[b->blockno % NBUCKETS];
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}

void
bpin(struct buf *b) {
  struct bucket *bucket = &bcache.buckets[b->blockno % NBUCKETS];
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  struct bucket *bucket = &bcache.buckets[b->blockno % NBUCKETS];
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}
