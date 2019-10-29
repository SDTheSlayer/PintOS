#ifndef VM_PAGE
#define VM_PAGE

#include <hash.h>
#include "filesys/off_t.h"
#include "filesys/file.h"


enum spte_type
  {
    CODE = 0, /* Only code is swappable. */
    FILE = 1, /* Read only executable file. */
    MMAP = 2  /* Files mapped to memory. */
  };

struct spt_entry
  {
    enum spte_type type;
    void *upage;
    void *frame;  /* kpage, if not NULL implies 
                     installed and loaded (or being loaded). */
    struct hash_elem elem;


    /* CODE */
    bool is_in_swap;

    /* FILE */
    struct file *file;
    off_t ofs;
    bool writable;
    uint32_t page_read_bytes;
    uint32_t page_zero_bytes;
  };


void supp_page_table_init (struct hash *);
struct spt_entry *uvaddr_to_spt_entry (void *);

bool grow_stack (void *);
bool create_spte_file (struct file *, off_t, uint8_t *,
                       uint32_t, uint32_t, bool);

#endif
