#include "threads/synch.h"
#include "threads/palloc.h"
#include <list.h>
#include "userprog/pagedir.h"
#include <bitmap.h>
#include "vm/swap.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <malloc.h>
#include "threads/malloc.h"

///*** VM01 ***///

/* This File has been created in VM part of the Project*/


/* Function Declarations */
static void *frame_alloc (enum palloc_flags);
static void add_to_frame_table (void *, struct spt_entry *);
static void clear_frame_entry (struct frame_table_entry *);
bool evict_frame (struct frame_table_entry *);
/* Frame table has been implemented in the form of Linked List */
static struct list frame_table;

/* Lock to edit frame table */ 
static struct lock frame_table_lock;

/*** Free frames finds the frame to be cleared in the frame table 
***/
void free_frame (void *frame)
{
  struct frame_table_entry *fte;
  struct list_elem *e;

  lock_acquire (&frame_table_lock);
  for (e = list_begin (&frame_table);
       e != list_end (&frame_table);
       e = list_next (e))
  {
    fte = list_entry (e, struct frame_table_entry, elem);
    if (fte->frame == frame)
    {
      list_remove (&fte->elem);
      break;
    }
  }
  lock_release (&frame_table_lock);

  palloc_free_page (frame);
}


/* Initialise frame Table*/
void frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

static struct frame_table_entry * get_victim_frame ()
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));
  struct list_elem *e;
  /***
  Frames in Main Memory are classified on the basis of Access and Modification (Dirty bit)
  00 --> neither recently used not modified – best page to replace
  01 --> not recently used but modified – not quite as good, must write out before replacement
  10 --> recently used but clean – probably will be used again soon
  11 --> recently used and modified – probably will be used again soon and need to write out before replacement

  ***/
  /* Phase 1: All dirty frames are written back to disk
              If 00 frame is found it is choosen as a victim */
  for (e = list_begin (&frame_table);e != list_end (&frame_table);e = list_next (e))
  {
    struct frame_table_entry *fte =
      list_entry (e, struct frame_table_entry, elem);
    bool is_dirty = pagedir_is_dirty (fte->t->pagedir,fte->spte->upage);
    bool is_accessed = pagedir_is_accessed (fte->t->pagedir,fte->spte->upage);

    if (!fte->spte->pinned)
    {
      if (fte->spte->type != CODE)
      {
        if (is_dirty)
        {
          if (write_to_disk (fte->spte))
            pagedir_set_dirty (fte->t->pagedir, fte->spte->upage, false);
        }
        else if (!is_accessed)
          return fte;
      }
      else
      {
        if (!is_dirty && !is_accessed)
          return fte;
      }
    }
  }

  /*** Phase 2: After previous phase all 01,11 --> 00,10
                Now access bit is reset
                If we find 00 then it is chosen as victim frame
   ***/
  for (e = list_begin (&frame_table);
       e != list_end (&frame_table);
       e = list_next (e))
  {
    struct frame_table_entry *fte =
      list_entry (e, struct frame_table_entry, elem);
    bool is_dirty = pagedir_is_dirty (fte->t->pagedir,fte->spte->upage);
    bool is_accessed = pagedir_is_accessed (fte->t->pagedir,fte->spte->upage);

    if (!fte->spte->pinned)
    {
      if ((!is_dirty || fte->spte->type == CODE) && !is_accessed)
        return fte;
      else //Accessed or (Dirty (FILE or MMAP)).
        pagedir_set_accessed (fte->t->pagedir, fte->spte->upage, false);
    }
  }

  /***
  Phase 3:
  All frames in memory are now of the type 00 so
  we find victim on the basis of FIFO 
  ***/
  ASSERT (!list_empty (&frame_table));
  for (e = list_begin (&frame_table);e != list_end (&frame_table);e = list_next (e))
  {
    struct frame_table_entry *fte = list_entry (e, struct frame_table_entry, elem);
    if (!fte->spte->pinned){
      return fte;
    }
  }
  return NULL;
}



bool evict_frame (struct frame_table_entry *fte)
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));
  struct spt_entry *spte = fte->spte;
  size_t idx;
  switch (spte->type){
  case MMAP:

    if (pagedir_is_dirty (fte->t->pagedir, spte->upage))
        if (!write_to_disk (spte))
        {
          PANIC ("Not able to write out");
          return false;
        }

    spte->frame = NULL;
    
    clear_frame_entry (fte);
    return true;
    break;
  case FILE:
    spte->type = CODE;
  case CODE:
    ASSERT (spte->frame != NULL);
    idx = swap_out (spte);
    if (idx == BITMAP_ERROR){
      PANIC ("Not able to swap out");
      return false;
    }

    spte->idx = idx;
    spte->is_in_swap = true;
    spte->frame = NULL;

    clear_frame_entry (fte);
    return true;
    break;
  default:
    PANIC ("Corrupt fte or spte");
    return false;
  }
  return true;
}



/*** Assign a frame to a page after checking if it is a valid request and a frame can be allocated
***/
void * get_frame_for_page (enum palloc_flags flags, struct spt_entry *spte)
{
  if(spte == NULL)
  {
    return NULL;
  }
  if (flags & PAL_USER == 0)
  {
    return NULL;
  }

  void *frame = frame_alloc (flags);

  if (frame == NULL)
  {
     PANIC ("Not able to get frame");
  }

  add_to_frame_table (frame, spte);
  return frame;
}


/*** Allocated a frame given the palloc flags (frame is also in essense stored on memeory hence we need a page for it as well)
***/
static void *
frame_alloc (enum palloc_flags flags)
{
if (flags & PAL_USER == 0)
    return NULL;

  void *frame = palloc_get_page (flags);
  if (frame != NULL)
    return frame;
  else
  {
    lock_acquire (&frame_table_lock);
    do {
      if (list_empty (&frame_table))
        PANIC ("palloc_get_page returned NULL when frame table empty.");

      struct frame_table_entry *fte = get_victim_frame ();

      ASSERT (fte != NULL);
      
      ASSERT (fte->spte->type < 3 && fte->spte->type >= 0 && fte->frame != NULL);
      ASSERT (fte->spte->frame != NULL);

      bool evicted = evict_frame (fte);
      if (evicted){
        frame = palloc_get_page (flags);
      }
      else
        PANIC ("Not able to evict. ");
    } while (frame == NULL);
      
    lock_release (&frame_table_lock);
    return frame;
  }
}

/* Adds the newly allocated frame to the frame table */
static void add_to_frame_table (void *frame, struct spt_entry *spte)
{
  struct frame_table_entry *fte = (struct frame_table_entry *) malloc (sizeof (struct frame_table_entry));

  lock_acquire (&frame_table_lock);

  fte->frame = frame;
  fte->spte = spte;
  ASSERT (fte->spte->type < 3 && fte->spte->type >= 0);
  fte->t = thread_current ();
  list_push_back (&frame_table, &fte->elem);

  lock_release (&frame_table_lock);
}


static void
clear_frame_entry (struct frame_table_entry *fte)
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));
  list_remove (&fte->elem);

  pagedir_clear_page (fte->t->pagedir, fte->spte->upage);
  palloc_free_page (fte->frame);
  free (fte);
}
