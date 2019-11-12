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

static void is_writable (const void *);
static bool is_valid_page (void *);
static void syscall_handler (struct intr_frame *);
static void valid_up (const void*, const void *);
static void close_file (int);
static bool is_valid_fd (int);
static void validate (const void*, const void *, size_t);
static void validate_string (const void *, const char *);



static void
unpin_buffer (void *buffer, unsigned size)
{
  uint32_t *pd = thread_current ()->pagedir;
  struct spt_entry *spte = uvaddr_to_spt_entry (buffer);
  if (spte != NULL)
    spte->pinned = false;
  spte = uvaddr_to_spt_entry (buffer + size -1);
  if (spte != NULL)
    spte->pinned = false;

  int i;
  for (i = PGSIZE; i < size; i += PGSIZE)
  {
    spte = uvaddr_to_spt_entry (buffer + i);
    if (spte != NULL)
      spte->pinned = false;
  }
}

static void
unpin_str (char *str)
{
  int l = strlen (str);
  unpin_buffer ((void *) str, l);
}













/************ UP02 ****************/
/*Terminates Pintos by calling shutdown_power_off() (declared in "threads/init.h"). 
  This should be seldom used, because you lose some information about possible 
  deadlock situations, etc. */
static int
halt (void *esp)
{
  power_off ();
}

/************ UP03 ****************/
/*Creates a new file called file initially initial_size bytes in size. 
  Returns true if successful, false otherwise. 
  Creating a new file does not open it: opening the new file is a 
  separate operation which would require a open system call. */
static int
create (void *esp)
{
  validate (esp, esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (esp, file_name);

  validate (esp, esp, sizeof(unsigned));
  unsigned initial_size = *((unsigned *) esp);
  esp += sizeof (unsigned);

  lock_acquire (&file_lock);
  int status = filesys_create (file_name, initial_size);
  lock_release (&file_lock);
  unpin_str (file_name);
  return status;
}

/************ UP03 ****************/
/*Deletes the file called file. Returns true if successful, false otherwise. 
A file may be removed regardless of whether it is open or closed, and removing an open file does not close it.*/
static int
remove (void *esp)
{
  validate (esp, esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (esp, file_name);

  lock_acquire (&file_lock);
  int status = filesys_remove (file_name);
  lock_release (&file_lock);
  unpin_str (file_name);
  return status;
}

/************ UP03 ****************/
/*Opens the file called file. Returns a nonnegative integer handle called 
  a "file descriptor" (fd), or -1 if the file could not be opened. */
static int
open (void *esp)
{
  validate (esp, esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (esp, file_name);
  
  lock_acquire (&file_lock);
  struct file *f = filesys_open (file_name);
  lock_release (&file_lock);

  if (f == NULL){
    unpin_str (file_name);
    return -1;
  }
  
  struct thread *t = thread_current ();

  int i;
  for (i = 2; i<MAX_FILES; i++)
  {
    if (t->files[i] == NULL){
      t->files[i] = f;
      break;
    }
  }

  int ret;
  if (i == MAX_FILES)
    ret = -1;
  else
    ret = i;
  unpin_str (file_name);
  return ret;
}

/************ UP02 ****************/
/*Terminates the current user program, returning status to the kernel. 
  If the process's parent waits for it (see below), 
  this is the status that will be returned. Conventionally, 
  a status of 0 indicates success and nonzero values indicate errors. */
int
exit (void *esp)
{
  int status = 0;
  if (!(esp != NULL)){
    status = -1;
  }
  else {
    validate (esp, esp, sizeof(int));
    status = *((int *)esp);
    esp += sizeof (int);

  }

  struct thread *t = thread_current ();

  int i;
  for (i = 2; i<MAX_FILES; i++)
  {
    if (t->files[i] != NULL){
      close_file (i);
    }
  }

  destroy_spt (&t->supp_page_table);
  
  char *name = t->name, *save;
  name = strtok_r (name, " ", &save);

  lock_acquire (&file_lock);
  printf ("%s: exit(%d)\n", name, status);
  lock_release (&file_lock);

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
  NOT_REACHED ();
}

/************ UP03 ****************/
/*Returns the size, in bytes, of the file open as fd. */
static int
filesize (void *esp)
{
  validate (esp, esp, sizeof(int));
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

/************ UP03 ****************/
/*Reads size bytes from the file open as fd into buffer. Returns the number of bytes actually 
  read (0 at end of file), or -1 if the file could not be read (due to a condition other than end of file). 
  Fd 0 reads from the keyboard using input_getc(). */
static int
read (void *esp)
{
 validate (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  validate (esp, esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);

  validate (esp, esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);
  
  validate (esp, buffer, size);

  struct thread *t = thread_current ();
  int ret = 0;
  if (fd == STDIN_FILENO)
  {
    lock_acquire (&file_lock);

    int i;
    for (i = 0; i<size; i++)
      *((uint8_t *) buffer+i) = input_getc ();

    lock_release (&file_lock);
    ret = i;
  }
  else if (is_valid_fd (fd) && fd >=2 && t->files[fd] != NULL)
  {
    is_writable (buffer);
    lock_acquire (&file_lock);
    int read = file_read (t->files[fd], buffer, size);
    lock_release (&file_lock);
    ret = read;
  }

  unpin_buffer (buffer, size);
  return ret;
}

/************ Modified in UP02 and UP03 ****************/
/*Writes size bytes from buffer to the open file fd. 
  Returns the number of bytes actually written, which 
  may be less than size if some bytes could not be written. */
static int
write (void *esp)
{
  validate (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  validate (esp, esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);

  validate (esp, esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);
  
  validate (esp, buffer, size);
  
  struct thread *t = thread_current ();
  int ret = 0;
  if (fd == STDOUT_FILENO)
  {
    lock_acquire (&file_lock);

    int i;
    for (i = 0; i<size; i++)
      putchar (*((char *) buffer + i));

    lock_release (&file_lock);
    ret = i;
  }
  else if (is_valid_fd (fd) && fd >=2 && t->files[fd] != NULL)
  {
    lock_acquire (&file_lock);
    int written = file_write (t->files[fd], buffer, size);
    lock_release (&file_lock);
    ret = written;
  }
    int status = 0;
  if (esp != NULL){
    validate (esp, esp, sizeof(int));
    status = *((int *)esp);
    esp += sizeof (int);
  }
  else {
    status = -1;
  }

  unpin_buffer (buffer, size);
  return ret;
}


/************ UP03 ****************/
/*Changes the next byte to be read or written in open file fd to position, 
  expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.) */
static int
seek (void *esp)
{
  validate (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  validate (esp, esp, sizeof(unsigned));
  unsigned position = *((unsigned *) esp);
  esp += sizeof (unsigned);

  struct thread *t = thread_current ();

  if (!(is_valid_fd (fd) && t->files[fd] != NULL))
  {

  }
  else{
    lock_acquire (&file_lock);
    file_seek (t->files[fd], position);
    lock_release (&file_lock);
  }
}

/************ UP03 ****************/
/*Returns the position of the next byte to be read or written in open file fd, expressed in bytes from the beginning of the file.*/
static int
tell (void *esp)
{
  validate (esp, esp, sizeof(int));
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

/************ UP04 ****************/
/*Runs the executable whose name is given in cmd_line, passing any given arguments, 
and returns the new process's program id (pid). Must return pid -1, which otherwise 
should not be a valid pid, if the program cannot load or run for any reason. Thus, 
the parent process cannot return from the exec until it knows whether the child process 
successfully loaded its executable. You must use appropriate synchronization to ensure this. */
static int
exec (void *esp)
{
  validate (esp, esp, sizeof (char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  validate_string (esp, file_name);

  lock_acquire (&file_lock);
  tid_t tid = process_execute (file_name);
  lock_release (&file_lock);
  
  struct thread *child = get_child_thread_from_id (tid);
  if (child == NULL)
  {
    unpin_str (file_name);
    return -1;
  }
  
  sema_down (&child->sema_ready);
  if (!child->load_complete)
    tid = -1;
  
  sema_up (&child->sema_ack);
  unpin_str (file_name);
  return tid;
}

/************ UP04 ****************/
/*Waits for a child process pid and retrieves the child's exit status. */
static int
wait (void *esp)
{
  validate (esp, esp, sizeof (int));
  int pid = *((int *) esp);
  esp += sizeof (int);

  struct thread *child = get_child_thread_from_id (pid);

  /* Either wait has already been called or 
     given pid is not a child of current thread. */
  if (child == NULL) 
    return -1;
    
  sema_down (&child->sema_terminated);
  int status = child->return_status;
  list_remove (&child->parent_elem);
  thread_unblock (child);
  return status;
}

/************ UP03 ****************/
/*Closes file descriptor fd. Exiting or terminating a process implicitly 
  closes all its open file descriptors, as if by calling this function for each one. */
static int
close (void *esp)
{
  validate (esp, esp, sizeof(int));
  int fd = *((int *) esp);
  esp += sizeof (int);

  if (is_valid_fd (fd))
    close_file (fd);
}

/************ VM02 ****************/
/* Memory maps an already opened file to the address value that is passed in the stack while calling mmap*/
static int
mmap (void *esp)
{
  validate (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  if (!is_valid_fd (fd))
    return -1;
  
  validate (esp, esp, sizeof(void *));
  const void *address = *((void **) esp);
  esp += sizeof (void *);
  
  if (!is_valid_page (address))
    return -1;

  struct thread *t = thread_current();
  struct file* old = t->files[fd];

  if (old == NULL)
    return -1;

  struct file *f = file_reopen (old);
  if (f == NULL)
    return -1;
  
  lock_acquire (&file_lock);
  int size = file_length (f);
  lock_release (&file_lock);

  struct spt_entry *spte = create_spte_mmap (f, size, address);
  if (spte == NULL)
    return -1;
  
  int i;
  for (i = 0; i<MAX_FILES; i++)
  {
    if (t->mmap_files[i] == NULL){
      t->mmap_files[i] = spte;
      break;
    }
  }

  if (i == MAX_FILES)
    return -1;
  else
    return i;
}

/************ VM02 ****************/
/*Unmaps a memory mapped file and frees the area in the main memory*/
static int
munmap (void *esp)
{
  validate (esp, esp, sizeof(int));
  int map_id = *((int *)esp);
  esp += sizeof (int);

  if (is_valid_fd (map_id)){
    
    struct thread *t = thread_current();
    struct spt_entry *spte = t->mmap_files[map_id];

    if (spte != NULL)
      free_spte_mmap (spte);
  }
}

/************ VM02 ****************/
/* Below syscalls were not asked to implement in the tasks but
  they were mentioned in the definition of syscall in pintdoc*/
static int
chdir (void *esp)
{
  exit (NULL);
}

static int
mkdir (void *esp)
{
  exit (NULL);
}

static int
readdir (void *esp)
{
  exit (NULL);
}

static int
isdir (void *esp)
{
  exit (NULL);
}

static int
inumber (void *esp)
{
  exit (NULL);
}




/************ UP02 ****************/
/* List of the system calls  */
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
    close,

    mmap,
    munmap,

    chdir,
    mkdir,
    readdir,
    isdir,
    inumber
  };

const int num_calls = sizeof (syscalls) / sizeof (syscalls[0]);

void
syscall_init (void) 
{
  lock_init (&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall"); 
}

/************ UP02 ****************/
/* Function that handles the system calls and maps the system call corresponding to their position in the array */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *esp = f->esp;

  validate (esp, esp, sizeof(int));
  int syscall_num = *((int *) esp);
  esp += sizeof(int);

  /* printf("\nSys: %d", syscall_num); */

  /* Just for sanity, we will anyway be checking inside all functions. */ 
  validate (esp, esp, sizeof(void *));

  if (syscall_num >= 0 && syscall_num < num_calls)
  {
    int (*function) (void *) = syscalls[syscall_num];
    int ret = function (esp);
    f->eax = ret;
  }
  else
  {
    printf ("\nError, invalid syscall number.");
    exit (NULL);
  }
  unpin_buffer (f->esp, sizeof (esp));
}

/************ UP03 ****************/
/* goes and closes the file mapped to the fd given */
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



/* Sanity check on the value of the fd such that it lies in between 0 and 128*/
static bool
is_valid_fd (int fd)
{
  return fd >= 0 && fd < MAX_FILES; 
}


/* Validates the string using validate function */
static void
validate_string (const void *esp, const char *s)
{
  validate (esp, s, sizeof(char));
  while (*s != '\0')
    validate (esp, s++, sizeof(char));
}


/* Uses the valid up function and checks the space from ptr to ptr+ size */
static void
validate (const void *esp, const void *ptr, size_t size)
{
  valid_up (esp, ptr);
  if(size != 1)
    valid_up (esp, ptr + size - 1);

  int i;
  for (i = PGSIZE; i < size; i += PGSIZE)
    valid_up (esp, ptr + i);
}





/* Basic function that validates the virtual address. Ensures that it is a valid address and tries loading a page if needed. Very similar to the 
page fault function implemented*/
static void
valid_up (const void *esp, const void *ptr)
{
  uint32_t *pd = thread_current ()->pagedir;
  if (ptr == NULL || !is_user_vaddr (ptr))
  {
    exit (NULL);
  }
  
  struct spt_entry *spte = uvaddr_to_spt_entry (ptr);
  if (spte != NULL)
  {
    spte->pinned = true;
    if (pagedir_get_page (pd, ptr) == NULL)
      if(!install_load_page (spte))
        exit (NULL);
  }
  else if (pagedir_get_page (pd, ptr) == NULL)
  {
    if(!(ptr >= esp - STACK_HEURISTIC &&
         grow_stack (ptr, true)))
      exit (NULL);
  }
}


/* Sanity check and checks if the page is aligned*/
static bool
is_valid_page (void *upage)
{
  /* non-zero */
  if (upage == 0)
    return false;
  
  /* Page aligned */
  if ((uintptr_t) upage % PGSIZE != 0)
    return false;

  return true;
}



/* Checks if the virtual address is writable*/
static void
is_writable (const void *ptr)
{
  struct spt_entry *spte = uvaddr_to_spt_entry (ptr);
  if (spte->type == FILE && !spte->writable)
    exit (NULL);
}
