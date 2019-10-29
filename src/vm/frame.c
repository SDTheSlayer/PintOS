#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <malloc.h>

struct list frame_table;
struct lock frame_table_lock;

void
frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

void *
get_frame_for_page (enum palloc_flags flags, struct spt_entry *spte)
{
  if (flags & PAL_USER == 0)
    return NULL;

  void *frame = frame_alloc (flags);

  if (frame != NULL && spte != NULL){
    add_to_frame_table (frame, spte);
    return frame;
  }
  else return NULL;
}

static void
add_to_frame_table (void *frame, struct spt_entry *spte) {
  struct frame_table_entry *fte =
    (struct frame_table_entry *) malloc (sizeof (struct frame_table_entry));
lock_acquire (&frame_table_lock);
  fte->frame = frame;
  fte->spte = spte;
  fte->t = thread_current ();
  list_push_back (&frame_table, &fte->elem);
  lock_release (&frame_table_lock);
}

//TODO:: Make static
/* Returns kernel virtual address of a kpage taken from user_pool. */
void *
frame_alloc (enum palloc_flags flags)
{
  if (flags & PAL_USER == 0)
    return NULL;

  void *frame = palloc_get_page (flags);
  if (frame != NULL)
    return frame;
  else
  {
    //TODO:: add frame eviction logic
    return NULL;
  }
}

void
free_frame (void *frame)
{
  palloc_free_page (frame);
}