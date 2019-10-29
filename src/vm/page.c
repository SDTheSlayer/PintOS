#include "vm/page.h"
#include <malloc.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "filesys/file.h"


/*
  Naming convention for pages and frames:
  
  frame, kpage => always installed at PD, PT
  (associated with a kernel virtual address taken from user_pool)
  only free is available

  upage => referenced by SPT of the parent thread.
  may be uninstalled or installed (loaded) to a frame (kpage) 
  file means readonly executable file
  
  others are mmap files
*/

struct hash supp_page_table;
struct lock spt_lock;

unsigned
spt_hash_func (const struct hash_elem *element, void *aux)
{
  struct spt_entry *spte = hash_entry (element, struct spt_entry, elem);
  return hash_int ((int) spte->upage);
}

bool
spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  struct spt_entry *spte_a = hash_entry (a, struct spt_entry, elem);
  struct spt_entry *spte_b = hash_entry (b, struct spt_entry, elem);

  return (int) spte_a->upage < (int) spte_b->upage;
}

void
supp_page_table_init (struct hash *supp_page_table)
{
  hash_init (&supp_page_table, spt_hash_func, spt_less_func, NULL);
  
}

struct spt_entry *
uvaddr_to_spt_entry (void *uvaddr)
{
  void *upage = pg_round_down (uvaddr);
  struct spt_entry spte;
  spte.upage = upage;

  struct hash_elem *e = hash_find (
    &thread_current()->supp_page_table, &spte.elem);

  if (e)
    return hash_entry (e, struct spt_entry, elem);
  else
    return NULL;
}







static struct spt_entry *
create_spte ()
{
  struct spt_entry *spte = (struct spt_entry *) malloc (
    sizeof (struct spt_entry));
  spte->frame = NULL;
  spte->upage = NULL;
  return spte;
}

struct spt_entry *
create_spte_code (void *upage)
{
  struct spt_entry *spte = create_spte ();
  spte->type = CODE;
  spte->upage = upage;
  hash_insert (&((thread_current())->supp_page_table), &spte->elem);
  return spte;

}

bool
create_spte_file (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct spt_entry *spte = create_spte ();
      spte->type = FILE;
      spte->upage = upage;
      spte->page_read_bytes = page_read_bytes;
      spte->page_zero_bytes = page_zero_bytes;
      spte->file = file;
      spte->ofs = ofs;
      spte->writable = writable;
      ofs += page_read_bytes;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;

      hash_insert (&((thread_current())->supp_page_table), &spte->elem);
    }
  return true;



}

void
create_spte_mmap ()
{

}

bool
install_load_file (struct spt_entry *spte)
{
  void *frame = get_frame_for_page (PAL_USER, spte);

  if (frame == NULL)
    return false;

  /* Load this page. */
  lock_acquire (&file_lock);
  file_seek (spte->file, spte->ofs);
  int read_bytes = file_read (spte->file, frame, spte->page_read_bytes);
  lock_release (&file_lock);

  if (read_bytes != (int) spte->page_read_bytes)
  {
    free_frame (frame);
    return false; 
  }
  memset (frame + spte->page_read_bytes, 0, spte->page_zero_bytes);

  /* Add the page to the process's address space. */
  if (!install_page (spte->upage, frame, spte->writable)) 
  {
    free_frame (frame);
    return false; 
  }
  return true;



}

bool
load_mmap (struct spt_entry *spte)
{

}

bool
load_swap (struct spt_entry *spte)
{

}

bool
install_load_page (struct spt_entry *spte)
{
  switch (spte->type){
  case FILE:
    return install_load_file (spte);
    break;
  case MMAP:
    return load_mmap (spte);
    break;
  case CODE:
    return load_swap (spte);
    break;
  default:
    return false;
  }
}

void
free_spte (struct hash_elem *e, void *aux)
{
  //TODO::
  // If mmap file and is dirty then write to disk
  // is stack or read only file then no need
  struct spt_entry *spte = hash_entry (e, struct spt_entry, elem);
  if (spte->frame != NULL)
    free_frame (spte->frame);
  free (spte);
}

void destroy_spt (){

  hash_destroy (&supp_page_table, free_spte);

}

