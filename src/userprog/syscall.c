#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

static void exit (int);
static int write (int, const void *, unsigned);

static void verify_uaddr (const void *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *esp = f->esp;

  verify_uaddr(esp);

  switch (*esp)
    {
      case SYS_EXIT:
        verify_uaddr(esp + 1);
        exit (*(esp + 1));
        break;

      case SYS_WRITE:
        verify_uaddr(esp + 1);
        verify_uaddr(esp + 2);
        verify_uaddr(esp + 3);
        f->eax = write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;

      default:
        break;
    }
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

/* Verifies the validity of an user-provided pointer.
   If the pointer is a null pointer, a pointer to unmapped virtual
   memory, or a pointer to kernel virtual address space (above
   PHYS_BASE), the pointer will be rejected and the offending
   process will be terminated and its resources freed. */
static void
verify_uaddr (const void *uaddr)
{
  if (uaddr == NULL || uaddr < (void *) 0x08048000 || uaddr >= PHYS_BASE)
    exit (-1);
}
