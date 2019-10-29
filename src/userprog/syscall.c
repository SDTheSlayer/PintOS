 
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "filesys/filesys.h"

/** UP03 **/
#include "threads/synch.h"
/* Lock for file system calls. */
//static struct lock file_lock;
static void validate (const void *, size_t);
static void validate_string (const char *);
static void close_file (int);
static int is_valid_fd (int);
/** UP03 **/

static void syscall_handler (struct intr_frame *);
static void valid_up (const void *);

static int
halt (void *esp)
{
  power_off();
}

int
exit (void *esp)
{
  int status = 0;
  if (esp != NULL){
    validate (esp, sizeof(int));
    status = *((int *)esp);
    esp += sizeof (int);
  }
  else {
    status = -1;
  }

  struct thread *t = thread_current ();

  int i;
  for (i = 2; i<MAX_FILES; i++)
  {
    if (t->files[i] != NULL){
      close_file (i);
    }
  }

  
  char *name = t->name, *save;
  name = strtok_r (name, " ", &save);
  
  printf ("%s: exit(%d)\n", name, status);
   t->return_status = status;

  /* Preserve the kernel struct thread just deallocate user page.
     struct thread will be deleted once parent calls wait or parent terminates.*/
  process_exit ();

  enum intr_level old_level = intr_disable ();
  t->no_yield = true;
  sema_up (&t->sema_terminated);
  
  thread_block ();
  intr_set_level (old_level);

  thread_exit ();
}

static int
exec (void *esp)
{ 
   validate (esp, sizeof (char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (file_name);
   lock_acquire (&file_lock);
  tid_t tid = process_execute (file_name);
  lock_release (&file_lock);

  struct thread *child = get_child_thread_from_id (tid);
  if (child == NULL)
    return -1;

  sema_down (&child->sema_ready);
  if (!child->load_complete)
    tid = -1;
  return tid;
} 

static int
wait (void *esp)
{
  validate (esp, sizeof (int));
  int pid = *((int *) esp);
  esp += sizeof (int);
  struct thread *child = get_child_thread_from_id (pid);
 /* Either wait has already been called or 
     given pid is not a child of current thread. */
  if (child == NULL) 
    return -1;
  list_remove (&child->parent_elem);
  sema_down (&child->sema_terminated);
  int status = child->return_status;

  thread_unblock (child);
  return status;
}

static int
create (void *esp)
{
  validate (esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (file_name);

  validate (esp, sizeof(unsigned));
  unsigned initial_size = *((unsigned *) esp);
  esp += sizeof (unsigned);

  lock_acquire (&file_lock);
  int status = filesys_create (file_name, initial_size);
  lock_release (&file_lock);

  return status;
}

static int
remove (void *esp)
{
  validate (esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (file_name);

  lock_acquire (&file_lock);
  int status = filesys_remove (file_name);
  lock_release (&file_lock);

  return status;
}

static int
open (void *esp)
{

  validate (esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (file_name);

  lock_acquire (&file_lock);
  struct file *f = filesys_open (file_name);
  lock_release (&file_lock);

  if (f == NULL)
    return -1;

  struct thread *t = thread_current ();

  int i;
  for (i = 2; i<MAX_FILES; i++)
  {
    if (t->files[i] == NULL){
      t->files[i] = f;
      break;
    }
  }

  if (i == MAX_FILES)
    return -1;
  else
    return i;

}

static int
filesize (void *esp)
{
  validate (esp, sizeof(int));
  int fd = *((int *) esp);
  esp += sizeof (int);

  struct thread *t = thread_current ();

  if (is_valid_fd (fd) && t->files[fd] != NULL)
  {  
    lock_acquire (&file_lock);
    int size = file_length (t->files[fd]);
    lock_release (&file_lock);

    return size;
  }
  return -1;
}

static int
read (void *esp)
{
  validate (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  validate (esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);

  validate (esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);

  validate (buffer, size);

  struct thread *t = thread_current ();
  if (fd == STDIN_FILENO)
  {
    lock_acquire (&file_lock);

    int i;
    for (i = 0; i<size; i++)
      *((uint8_t *) buffer+i) = input_getc ();

    lock_release (&file_lock);
    return i;
  }
  else if (is_valid_fd (fd) && fd >=2 && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    int read = file_read (t->files[fd], buffer, size);
    lock_release (&file_lock);
    return read;
  }
  return 0;
}

static int
write (void *esp)
{
  validate (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  validate (esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);

  validate (esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);
  
  validate (buffer, size);

  struct thread *t = thread_current ();

  if (fd == STDOUT_FILENO)
  {
    lock_acquire (&file_lock);
    int i;
    for (i = 0; i<size; i++)
    {
      putchar (*((char *) buffer + i));
    }
    lock_release (&file_lock);
    return i;
  }

  else if (is_valid_fd (fd) && fd >=2 && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    int written = file_write (t->files[fd], buffer, size);
    lock_release (&file_lock);
    return written;
  }

  return 0;
}

static void
seek (void *esp)
{
  validate (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  validate (esp, sizeof(unsigned));
  unsigned position = *((unsigned *) esp);
  esp += sizeof (unsigned);

  struct thread *t = thread_current ();

  if (is_valid_fd (fd) && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    file_seek (t->files[fd], position);
    lock_release (&file_lock);
  }
}

static int
tell (void *esp)
{
  validate (esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  struct thread *t = thread_current ();

  if (is_valid_fd (fd) && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    int position = file_tell (t->files[fd]);
    lock_release (&file_lock);
    return position;
  }

  return -1;
}

static void
close (void *esp)
{
  validate (esp, sizeof(int));
  int fd = *((int *) esp);
  esp += sizeof (int);

  if (is_valid_fd (fd))
  close_file (fd);
}

static int (*syscalls []) (void *) =
  {
    halt,
    exit,
    exec,
    wait,
    create,
    remove,
    open,
    filesize,
    read,
    write,
    seek,
    tell,
    close
  };

const int num_calls = sizeof (syscalls) / sizeof (syscalls[0]);

void
syscall_init (void) 
{
  lock_init (&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall"); 
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *esp = f->esp;

  validate (esp, sizeof(int));
  int syscall_num = *((int *) esp);
  esp += sizeof(int);

  validate (esp, sizeof(void *));
  if (syscall_num >= 0 && syscall_num < num_calls)
  {
    int (*function) (void *) = syscalls[syscall_num];
    int ret = function (esp);
    f->eax = ret;
  }
  else
  {
    printf ("\nError, invalid syscall number.");
    thread_exit ();
  }
}

/** UP03 **/


static void
close_file (int fd)
{
  struct thread *t = thread_current ();
  if (t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    file_close (t->files[fd]);
    t->files[fd] = NULL;
    lock_release (&file_lock);
  }
}

static int
is_valid_fd (int fd)
{
  return fd >= 0 && fd < MAX_FILES; 
}


static void
validate_string (const char *s)
{
  validate (s, sizeof(char));
  while (*s != '\0')
    validate (s++, sizeof(char));
}

static void
validate (const void *ptr, size_t size)
{
  valid_up (ptr);
  if(size != 1)
    valid_up (ptr + size - 1);
}

/** UP03 **/


static void
valid_up (const void *ptr)
{
  uint32_t *pd = thread_current ()->pagedir;
  if ( ptr == NULL || !is_user_vaddr (ptr) || pagedir_get_page (pd, ptr) == NULL)
  {
    exit (NULL);
  }
}
