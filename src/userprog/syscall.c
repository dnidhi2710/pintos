#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
static void check_ptr (const void *);

/* Lock to synchronize access to the file system. */
static struct lock fs_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&fs_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *esp = f->esp;

  check_ptr (esp);
  switch (*esp)
    {
      case SYS_HALT:
        halt ();
        break;

      case SYS_EXIT:
        check_ptr (esp + 1);
        exit (*(esp + 1));
        break;

      case SYS_EXEC:
        check_ptr (esp + 1);
        f->eax = exec ((char *) *(esp + 1));
        break;

      case SYS_WAIT:
        check_ptr (esp + 1);
        f->eax = wait (*(esp + 1));
        break;

      case SYS_CREATE:
        check_ptr (esp + 1);
        check_ptr (esp + 2);
        f->eax = create ((char *) *(esp + 1), *(esp + 2));
        break;

      case SYS_REMOVE:
        check_ptr (esp + 1);
        f->eax = remove ((char *) *(esp + 1));
        break;

      case SYS_OPEN:
        check_ptr (esp + 1);
        f->eax = open ((char *) *(esp + 1));
        break;

      case SYS_FILESIZE:
        check_ptr (esp + 1);
        f->eax = filesize (*(esp + 1));
        break;

      case SYS_READ:
        check_ptr (esp + 1);
        check_ptr (esp + 2);
        check_ptr (esp + 3);
        f->eax = read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;

      case SYS_WRITE:
        check_ptr (esp + 1);
        check_ptr (esp + 2);
        check_ptr (esp + 3);
        f->eax = write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;

      case SYS_SEEK:
        check_ptr (esp + 1);
        check_ptr (esp + 2);
        seek (*(esp + 1), *(esp + 2));
        break;

      case SYS_TELL:
        check_ptr (esp + 1);
        f->eax = tell (*(esp + 1));
        break;

      case SYS_CLOSE:
        check_ptr (esp + 1);
        close (*(esp + 1));
        break;

      default:
        break;
    }
}


void
halt (void)
{
  shutdown_power_off ();
}

void
exit (int status)
{
  process_set_status (status);
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

pid_t
exec (const char *cmd_line)
{
  check_ptr (cmd_line);

  pid_t pid = -1;

  lock_acquire (&fs_lock);
  pid = process_execute (cmd_line);
  lock_release (&fs_lock);

  return pid;
}

int
wait (pid_t pid)
{
  return process_wait (pid);
}

bool
create (const char *file, unsigned initial_size)
{
  check_ptr (file);

  bool success = false;

  lock_acquire (&fs_lock);
  success = filesys_create (file, initial_size);  
  lock_release (&fs_lock);

  return success;
}

bool
remove (const char *file)
{
  check_ptr (file);

  bool success = false;

  lock_acquire (&fs_lock);
  success = filesys_remove (file);
  lock_release (&fs_lock);

  return success;
}

int
open (const char *file)
{
  check_ptr (file);

  int fd = -1;

  lock_acquire (&fs_lock);
  struct file *f = filesys_open (file);
  if (f != NULL)
    {
      fd = process_add_file (f);
    }
  lock_release (&fs_lock);

  return fd;
}

int
filesize (int fd)
{
  int size = 0;

  lock_acquire (&fs_lock); 
  struct file *f = process_get_file (fd);
  if (f != NULL)
    size = file_length (f);
  lock_release (&fs_lock);

  return size;
}

int
read (int fd, void *buffer, unsigned size)
{
  check_ptr (buffer);
  char *ptr = (char *) buffer;
  for (unsigned i = 0; i < size; i++)
    check_ptr(ptr++);

  int bytes = 0;

  if (fd == STDIN_FILENO)
    {
      char *c = buffer;

      for (unsigned i = 0; i < size; i++)
	      {
          *c = input_getc ();
          c++;
	      }

      bytes = size;
    }

  if (fd > 1)
    {
      lock_acquire (&fs_lock); 
      struct file *f = process_get_file (fd);
      if (f != NULL)
        bytes = file_read (f, buffer, size);
      lock_release (&fs_lock);
    }

  return bytes;
}

int
write (int fd, const void *buffer, unsigned size)
{
  check_ptr (buffer);
  char *ptr = (char *) buffer;
  for (unsigned i = 0; i < size; i++)
    check_ptr(ptr++);

  int bytes = 0;

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      bytes = size;
    }

  if (fd > 1)
    {
      lock_acquire (&fs_lock); 
      struct file *f = process_get_file (fd);
      if (f != NULL)
        bytes = file_write (f, buffer, size);
      lock_release (&fs_lock);
    }

  return bytes;
}

void
seek (int fd, unsigned position)
{
  lock_acquire (&fs_lock); 
  struct file *f = process_get_file (fd);
  if (f != NULL)
    file_seek (f, position);
  lock_release (&fs_lock);
}

unsigned
tell (int fd)
{
  int bytes = 0;

  lock_acquire (&fs_lock); 
  struct file *f = process_get_file (fd);
  if (f != NULL)
    bytes = file_tell (f);
  lock_release (&fs_lock);

  return bytes;
}

void
close (int fd)
{
  lock_acquire (&fs_lock); 
  struct file *f = process_remove_file (fd);
  if (f != NULL)
    file_close (f);
  lock_release (&fs_lock);
}

/* Ensures that the user-provided pointer is within the user
   virtual address space. If the pointer is invalid, the offending
   process will be terminated and its resources freed. */
static void
check_ptr (const void *ptr)
{
  if (is_kernel_vaddr (ptr))
    exit (-1);

  if (pagedir_get_page (thread_current ()->pagedir, ptr) == NULL)
    exit (-1);
}
