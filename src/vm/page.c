#include "vm/page.h"
#include <malloc.h>
#include <bitmap.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "filesys/file.h"


/* Declarations of Local Functions*/

/*VM01*/
static struct spt_entry* create_spte ();
static bool install_load_file (struct spt_entry *);
static void free_spte_elem (struct hash_elem *, void *);
static void free_spte (struct spt_entry *);

/*VM02*/
static bool install_load_mmap (struct spt_entry *);
static bool install_load_swap (struct spt_entry *);


/*** VM01 Creating Supp Page table (hash table) , adding , ddeleteing entries ***/
/** SPTE Functions**/



/* Creating new SPTE Entry */
static struct spt_entry * create_spte ()
{
  struct spt_entry *spte = (struct spt_entry *) malloc ( sizeof (struct spt_entry));
  spte->frame = NULL;
  spte->upage = NULL;
  /*VM03 Checking if present in swap*/
  spte->is_in_swap = false;
  spte->idx = BITMAP_ERROR;
  spte->pinned = false;
  return spte;
}


/* Helper function to find spte entry from hash table passing it to free_spte() */
static void free_spte_elem (struct hash_elem *e, void *aux)
{
  struct spt_entry *spte = hash_entry (e, struct spt_entry, elem);
  free_spte (spte);
}


/* Freeing spte and if mmap or writeable file then writing back to disk , Changed in VM02*/
static void free_spte (struct spt_entry *spte)
{
  if (spte != NULL)
  {
    /*If the Page has been allocated a frame remove it */
    if (spte->frame != NULL)
    {
      /*Writes back to disk if possible*/
      if(spte->type == MMAP || (spte->type == FILE && spte->writable))
      {
        write_to_disk (spte);
      }
      /*Removing the entry from page table and frame table*/
      void *pd = thread_current()->pagedir;
      pagedir_clear_page (pd, spte->upage);
      free_frame (spte->frame);
    }
    /* Removes entry from Supp page table*/
    hash_delete (&thread_current()->supp_page_table, &spte->elem);
    free (spte);
  }
}

/* Used to erase the complete Supp Page Table */
void destroy_spt (struct hash *supp_page_table){
  hash_destroy (supp_page_table, free_spte_elem);
}


/*VM01 Hash Helper Functions */

/*Hash Function used for Supp page Table is the hash of the address of upage*/
unsigned spt_hash_func (const struct hash_elem *element, void *aux UNUSED)
{
  struct spt_entry *spte = hash_entry (element, struct spt_entry, elem);
  // returns the built in hash function of upage of the page 
  return hash_int ((int) spte->upage); 
}

/*Compareator for hash entries based on upage(hash variable) */
bool spt_less_func (const struct hash_elem *a, const struct hash_elem *b,void *aux UNUSED)
{
  struct spt_entry *spte_a = hash_entry (a, struct spt_entry, elem);
  struct spt_entry *spte_b = hash_entry (b, struct spt_entry, elem);

  return (int) spte_a->upage < (int) spte_b->upage;
}

/*Initialising Hash table (in-built Function) */
void supp_page_table_init (struct hash *supp_page_table)
{
  hash_init (supp_page_table, spt_hash_func, spt_less_func, NULL);
}


/*Converts user virtual address to spte entry using page to which the address belongs*/
struct spt_entry * uvaddr_to_spt_entry (void *uvaddr)
{
  struct spt_entry spte;
  void *upage = pg_round_down (uvaddr);
  spte.upage = upage;
  // Searches in supp page table using the upage
  struct hash_elem *e = hash_find ( &thread_current()->supp_page_table, &spte.elem);
  if (e==NULL)
  {
    return NULL;
  }
  //Returns original spte entry if found
  return hash_entry (e, struct spt_entry, elem);
}


/*Loads the page depending on the type of spte entry*/
bool install_load_page (struct spt_entry *spte)
{
  switch (spte->type)
  {
    case FILE:  return install_load_file (spte);
    case MMAP:  return install_load_mmap (spte);
    case CODE:  return install_load_swap (spte);
    default:    return false;
  }
}


/* Creating Spte entry for Code pages*/
struct spt_entry * create_spte_code (void *upage)
{
  struct spt_entry *spte = create_spte ();
  spte->type = CODE;
  spte->upage = upage;
  hash_insert (&((thread_current())->supp_page_table), &spte->elem);
  return spte;
}



/* Loads file onto memory*/
static bool install_load_file (struct spt_entry *spte)
{
  // Allocates frame so as to load the page onto physical memory
  void *cur_frame = get_frame_for_page (PAL_USER, spte);
  ASSERT (cur_frame != NULL);
  // If frame loading fails , return false
  if (cur_frame == NULL)
  {
    return false;
  }

  // Acquires lock and reads the speicified no of bytes
  lock_acquire (&file_lock);
    file_seek (spte->file, spte->ofs);
    int read_bytes = file_read (spte->file, cur_frame, spte->page_read_bytes);
  lock_release (&file_lock);
  
  // Make all the extra bytes zero
  memset (cur_frame + spte->page_read_bytes, 0, spte->page_zero_bytes);

  // If the specified no of bytes cant be read then , free the allocated frame and return false
  if (read_bytes != (int) spte->page_read_bytes)
  {
    free_frame (cur_frame);
    return false; 
  }

  // Tries to install page into the page table, if fails then return flase
  if (!install_page (spte->upage, cur_frame, spte->writable)) 
  {
    free_frame (cur_frame);
    return false; 
  }

  spte->frame = cur_frame;
  return true;
}


/*VM01 Lazy loading : Loads a file when needed , reads file and loads it into the necessary no of pages*/
bool create_spte_file (struct file *file, off_t ofs, uint8_t *upage,uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0) 
  {
    //if bytes to read is greater than page size , then read only PGSIZE bytes
    size_t page_read_bytes = read_bytes;
    if(page_read_bytes > PGSIZE)
    {
      page_read_bytes=PGSIZE;
    }
    // Leftover bytes , needed to be made 0
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /*Initialise all the variables of spte entry*/
    struct spt_entry *spte = create_spte ();
    spte->page_read_bytes = page_read_bytes;
    spte->page_zero_bytes = page_zero_bytes;
    spte->type = FILE;
    spte->upage = upage;
    spte->ofs = ofs;
    spte->writable = writable;
    spte->file = file;

    // Changes new ofset of file to the final offset after reading page_read_bytes
    ofs += page_read_bytes;
    
    // Bytes left to read
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;

    // Used to allocate next page if needed
    upage += PGSIZE;

    // Insert the spte entry into table
    hash_insert (&((thread_current())->supp_page_table), &spte->elem);
  }
  return true;
}



//** VM02 Memory Mapped Files **// 

/*helper Function for calling load file*/
static bool install_load_mmap (struct spt_entry *spte)
{
  return install_load_file (spte);
}





/*Maps the file onto a pages in the memory (very similar to file loading) and returns spte of first page*/
struct spt_entry * create_spte_mmap (struct file *f, int read_bytes, void *upage)
{
  struct thread *t = thread_current();
  struct spt_entry *first_spte;
  int ofs = 0;
  int i = 0;
  uint32_t page_read_bytes, page_zero_bytes;

  first_spte =NULL;
  
  while (read_bytes > 0)
  {
    //if bytes to read is greater than page size , then read only PGSIZE bytes
    page_read_bytes = read_bytes;
    if(page_read_bytes > PGSIZE)
    {
      page_read_bytes=PGSIZE;
    }
    // Leftover bytes , needed to be made 0
    page_zero_bytes = PGSIZE - page_read_bytes;

    struct spt_entry *spte = uvaddr_to_spt_entry (upage);
    /* If there already exists a page at the given address then the file could not be loaded*/
    /* And we need to remove all the pages that we did map*/
    if (spte != NULL){
      free_spte_mmap (first_spte);
      return NULL;
    }
    /*Initialise all the variables of spte entry*/
    spte = create_spte ();
    spte->type = MMAP;
    spte->upage = upage;
    spte->file = f;
    spte->ofs = ofs;
    spte->page_read_bytes = page_read_bytes;
    spte->page_zero_bytes = page_zero_bytes;
    spte->writable = true;
    // Changes new ofset of file to the final offset after reading page_read_bytes
    ofs += page_read_bytes;

    // Bytes left to read
    read_bytes -= page_read_bytes;
    upage += PGSIZE;
    // Insert the spte entry into table
    hash_insert (&(t->supp_page_table), &spte->elem);

    //Returns the first Supp page table entry
    if (i == 0)
    {
      first_spte = spte;
      i++;
    }
    
  }
  return first_spte;
}
  


/* Frees all spte entries allocated to the given file */
void free_spte_mmap (struct spt_entry *first_spte)
{
  if (first_spte != NULL)
  {
    void *upage = first_spte->upage;
    struct spt_entry *spte;
    int read_bytes = file_length (first_spte->file);
    // keeps deleting spte entries while the size of file is not reached and pages correspond to the same file
    while (read_bytes > 0)
    {
      spte = uvaddr_to_spt_entry (upage);
      upage += PGSIZE;
      read_bytes -= spte->page_read_bytes;
      if (spte->file == first_spte->file)
      {
        free_spte (spte);
      }
    }
  }
}


/* lazily Grows stack when needed*/
bool grow_stack (void *uaddr, bool pinned)
{
  void *upage = pg_round_down (uaddr);

  // checks if system defined max stack size has been reached
  if ((size_t) (PHYS_BASE - uaddr) > MAX_STACK_SIZE)
  {
    return false;
  }
  
  // Crestes Code Spte entry for the stack
  struct spt_entry *spte = create_spte_code (upage);
  spte->pinned = pinned;
  return install_load_page (spte);
}


/* Vm03 Swapping*/
static bool install_load_swap (struct spt_entry *spte)
{
  void *frame = get_frame_for_page (PAL_USER | PAL_ZERO, spte);
  ASSERT (frame != NULL);
  
  if (frame == NULL)
    return false;

  if (install_page (spte->upage, frame, true))
  {
    spte->frame = frame;
    if (spte->is_in_swap) /* Add empty page (stack growth). */
    {
      swap_in (spte);
      spte->is_in_swap = false;
      spte->idx = BITMAP_ERROR;
    }
    return true;
  }
  else
    free_frame (frame);

  return false;
}



/* Writes back to disk if the page is dirty and the file is writable*/
bool write_to_disk (struct spt_entry *spte)
{
  struct thread *t = thread_current ();
  if (pagedir_is_dirty (t->pagedir, spte->upage))
  {

    lock_acquire (&file_lock);
      off_t written = file_write_at (spte->file, spte->upage,spte->page_read_bytes, spte->ofs);
    lock_release (&file_lock);
    if (written != spte->page_read_bytes)
    {
      return false;
    }
  }
  return true;
}

