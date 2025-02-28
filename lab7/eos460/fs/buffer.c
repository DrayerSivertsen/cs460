
#include "../type.h"

struct buf buffer[NBUF];
struct buf *freelist;
struct semaphore freebuf;
int freelist_wanted = 0; // free list wanted flag

struct devtab devtab[NDEV];

int requests, hits;

struct buf readbuf, writebuf;  // 2 dedicated buffers

int binit()
{
  // add all buffers into the freelist
  freelist = buffer;
  struct buf *tmp = freelist;

  for (int i = 1; i < NBUF; i++)
  {
    tmp->next_free = &buffer[i];
    tmp = tmp->next_free;
  }

  // initialize lock and buf
  for (int i = 0; i < NBUF; i++)
  {
    buffer[i].buf = 700000 + i*1024;
    buffer[i].lock.value = 1;
  }

  // initialize freebuf
  freebuf.value = 1;
}

struct buf *indevlist(int dev, int blk)
{
  struct buf *bp = devtab[dev].dev_list;

  while (bp)
  {
    if (bp->dev == dev && bp->blk == blk) // bp in dev_list
    {
      return bp;
    } 
    bp = bp->next_dev;
  }
  return 0;
}

void removefreelist(struct buf *bp)
{
  struct buf *prev;
  struct buf *cur = freelist;
  while (cur)
  {
    if (cur == bp)
    {
      // remove bp from free list
      if (cur == freelist)
      {
        freelist = cur->next_free;
        cur->next_free = 0;
        return;
      }
      else
      {
        prev->next_free = cur->next_free;
        cur->next_free = 0;
        return;
      }
    }
    prev = cur;
    cur = cur->next_free;
  }
}

/* getblk: return a buffer=(dev,blk) for exclusive use */
struct buf *getblk(int dev, int blk)
{
  struct buf *bp;
  hits++;

  while (1)
  {
    requests++;
    P(freebuf); // get a free buffer first
    
    bp = indevlist(dev, blk); // check if bp in dev_list
    if (bp)
    {
      if (bp->busy == 0) // bp not busy
      {
        removefreelist(bp); // remove bp from free list
        P(bp->lock); // lock bp but does not wait
        return bp;
      }
      // bp in cache but BUSY
      V(freebuf); // give up the free buffer
      P(bp->lock); // wait in bp queue
      return bp;
    }
    // bp not in cache, try to create a bp=(dev, blk)
    bp = freelist;
    if (freelist)
      freelist = freelist->next_free;

    P(bp->lock);
    if (bp->dirty)
    {
      awrite(bp); // write bp out ASYNC, no wait
      continue;
    }

    // mark bp data invalid, not dirty
    bp->dev = dev;
    bp->blk = blk;
    bp->valid = 0;
    bp->dirty = 0;
    return bp;
  }
} 


struct buf *bread(int dev, int blk)
{
  struct buf *bp = getblk(dev, blk); // get a buffer for (dev,blk)

  if (bp->valid == 0) {
    bp->opcode = 0x18; // READ
    getblock(bp->blk, bp->buf);
    bp->valid = 1;
  }

  return bp;
}

int bwrite(struct buf *bp) // release bp marked VALID but DIRTY
{ 
  bp->valid = 1;
  bp->dirty = 1;
  brelse(bp);
}

int awrite(struct buf *bp) // write DIRTY bp out to disk
{
  bp->opcode = 0x25; // WRITE
  putblock(bp->blk, bp->buf);
  bp->dirty = 0; // turn off DIRTY flag
  brelse(bp); // bp VALID and in buffer cache
}

/* brelse: releases a buffer as FREE to freelist */
int brelse(struct buf *bp)
{
  // printf(" brelse bp\n");
  if (bp->lock.value < 0)
  {
    V(bp->lock);
    return;
  }
  if (bp->dirty && freebuf.value < 0)
  {
    awrite(bp);
    return;
  }

  struct buf *tmp = freelist;
  while (tmp && tmp->next_free)
  {
    tmp = tmp->next_free;
  }

  tmp->next_free = bp;
  
  V(bp->lock);
  V(freebuf);
  return;
}

int khits(int y, int z) // syscall hits entry
{
   *(int *)(y) = requests;
   *(int *)(z) = hits;
   return NBUF;  
}

