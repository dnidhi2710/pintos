#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

static void halt (void);
static void exit (int);
static int write (int, const void *, unsigned);

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

      case SYS_WRITE:
        f->eax = write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;

      default:
        break;
    }
}

/* Terminates Pintos by calling shutdown_power_off() (declared in
   `devices/shutdown.h'). This should be seldom used, because you lose
   some information about possible deadlock situations, etc. */
void
halt (void)
{
  shutdown_power_off ();
}

/* Terminates the current user program, returning status to the kernel.
   If the process's parent waits for it (see below), this is the status
   that will be returned. Conventionally, a status of 0 indicates
   success and nonzero values indicate errors. */
static void
exit (int status)
{
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

/* Writes size bytes from BUFFER to the open file FD. Returns the
   number of bytes actually written, which may be less than SIZE if
   some bytes could not be written. */
static int
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
