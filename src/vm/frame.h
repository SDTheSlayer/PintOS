#ifndef VM_FRAME
#define VM_FRAME

#include "threads/thread.h"
#include "threads/palloc.h"
#include <list.h>
#include "vm/page.h"


/*Structure for Frame table entry */

struct frame_table_entry
{
	struct spt_entry *spte;
	void *frame;
	struct list_elem elem;
	struct thread *t;
};


/* Global Function Declarations */
void free_frame (void *);
void frame_table_init (void);
void *get_frame_for_page (enum palloc_flags, struct spt_entry *);


#endif
