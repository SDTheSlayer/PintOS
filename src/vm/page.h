#ifndef VM_PAGE
#define VM_PAGE

#include <hash.h>

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
  };

#endif