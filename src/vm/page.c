#include "vm/page.h"
#include "threads/synch.h"

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
supp_page_table_init ()
{
  hash_init (&supp_page_table, spt_hash_func, spt_less_func, NULL);
  lock_init (&spt_lock);
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

void
create_spte_code (void *upage)
{
  struct spt_entry *spte = create_spte ();
  spte->type = CODE;
  spte->upage = upage;
  lock_acquire (&spt_lock);
  hash_insert (&supp_page_table, &spte->elem);
  lock_release (&spt_lock);
}

void
create_spte_file ()
{

}

void
create_spte_mmap ()
{

}

bool
load_file (struct spt_entry *spte)
{

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
    load_file (spte);
  case MMAP:
    load_mmap (spte);
  case CODE:
    load_swap (spte);
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
  lock_acquire (&spt_lock);
  hash_destroy (&supp_page_table, free_spte);
  lock_release (&spt_lock);
}