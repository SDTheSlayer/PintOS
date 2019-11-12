#ifndef VM_PAGE
#define VM_PAGE

#include <hash.h>
#include "filesys/off_t.h"
#include "filesys/file.h"

/* An enum to store the type of spte entry*/
enum spte_type
{
  CODE = 0, /* Code including stack pages etc */
  FILE = 1, /* Opening a executable file */
  MMAP = 2  /* Memory mapped files (VM02) */
};

/* Base Spte entry structure , implemented as a hash table entry*/ 
struct spt_entry
{
  /*VM01*/
  enum spte_type type;    // type of entry
  void *upage;            // user page
  void *frame;            // frame allocated
  struct hash_elem elem;  // Entry in hash table
   bool pinned;
  bool is_in_swap;        // For Code Entries
  size_t idx;

  /*VM01 - Lazy Loading*/
  struct file *file;      //File entry 
  off_t ofs;              //Offset from start of while , file needs to be loaded from this point
  bool writable;          // Needs to be writeable for write backs
  uint32_t page_read_bytes; // Checking how many bytes are actually read 
  uint32_t page_zero_bytes; // and how many are empty and have been set to 0

};

/* Global declaration of functions*/

/* VM01 */
void supp_page_table_init (struct hash *);
struct spt_entry *uvaddr_to_spt_entry (void *);
bool create_spte_file (struct file *, off_t, uint8_t *, uint32_t, uint32_t, bool);
void destroy_spt (struct hash *);

/* VM02 */
struct spt_entry* create_spte_mmap (struct file *, int, void *);
bool grow_stack (void *, bool);
void free_spte_mmap (struct spt_entry *);
bool write_to_disk (struct spt_entry *);

#endif