#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>
#include "filesys/file.h"
#include "threads/thread.h"

/* A file owned by an user process. */
struct process_file
  {
    int fd;                             /* File descriptor. */
    struct file *file;                  /* File struct. */
    struct list_elem elem;              /* List element. */
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

int process_add_file (struct file *);
struct file *process_remove_file (int fd);
struct file *process_get_file (int fd);

#endif /* userprog/process.h */
