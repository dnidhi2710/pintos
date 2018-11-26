#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void verify_stack (const void *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  verify_stack(f->esp);

  int *esp = f->esp;
  switch (*esp)
    {
      case SYS_HALT:
        halt ();
        break;

      case SYS_EXIT:
        exit (*(esp + 1));
        break;

      case SYS_EXEC:
        f->eax = exec ((char *) *(esp + 1));
        break;

      case SYS_WAIT:
        f->eax = wait (*(esp + 1));
        break;

      case SYS_CREATE:
        f->eax = create ((char *) *(esp + 1), *(esp + 2));
        break;

      case SYS_REMOVE:
        f->eax = remove ((char *) *(esp + 1));
        break;

      case SYS_OPEN:
        f->eax = open ((char *) *(esp + 1));
        break;

      case SYS_FILESIZE:
        f->eax = filesize (*(esp + 1));
        break;

      case SYS_READ:
        f->eax = read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;

      case SYS_WRITE:
        f->eax = write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;

      case SYS_SEEK:
        seek (*(esp + 1), *(esp + 2));
        break;

      case SYS_TELL:
        f->eax = tell (*(esp + 1));
        break;

      case SYS_CLOSE:
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
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

pid_t
exec (const char *cmd_line)
{
  return process_execute (cmd_line);
}

int
wait (pid_t pid)
{
  return process_wait(pid);
}

bool
create (const char *file, unsigned initial_size)
{
  return false;
}

bool
remove (const char *file)
{
  return false;
}

int
open (const char *file)
{
  return -1;
}

int
filesize (int fd)
{
  return 0;
}

int
read (int fd, void *buffer, unsigned size)
{
  return 0;
}

int
write (int fd, const void *buffer, unsigned size)
{
  int status = 0;

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      status = size;
    }

  return status;
}

void
seek (int fd, unsigned position)
{
}

unsigned
tell (int fd)
{
  return 0;
}

void
close (int fd)
{
}

/* Verifies the validity of the stack at ESP.
   If any pointer on the stack is invalid, the offending process will
   be terminated and its resources freed. */
static void
verify_stack (const void *esp)
{
  struct thread *cur = thread_current ();
  void *ptr;

  for (int i = 0; i < 4; i++)
    {
      ptr = (int *) esp + i;

      /* Make sure the pointer address is an user virtual address. */
      if (ptr == NULL || ptr < (void *) 0x08048000 || ptr >= PHYS_BASE)
        exit (-1);
      
      /* Covers the case where the last pointer positioned such that
         its first byte is valid but the remaining bytes of the data
         are in invalid memory (see the sc-boundary-3 test). */
      if (pagedir_get_page (cur->pagedir, ptr) == NULL)
        exit (-1);
    }
}
