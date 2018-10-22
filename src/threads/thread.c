#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. The list must
   always be sorted in descending order of priority. */
static struct list ready_list;

/* List of processes that are asleep. The list must always be
   sorted in ascending order of sleep ticks. */
static struct list sleep_list;

static struct list donations;
   
/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Function that will ensure that the list remains in ascending order
   of sleep timer ticks; see list_less_func in lib/kernel/list.h. */
static bool
sleep_list_less_func (const struct list_elem *a,
                      const struct list_elem *b,
                      void *aux UNUSED)
{
  struct thread *t_a = list_entry(a, struct thread, elem);
  struct thread *t_b = list_entry(b, struct thread, elem);

  return t_a->sleep_ticks < t_b->sleep_ticks;
}

/* Function that will ensure that the list remains in descending order
   of thread priorities; see list_less_func in lib/kernel/list.h. */
static bool
ready_list_less_func (const struct list_elem *a,
                      const struct list_elem *b,
                      void *aux UNUSED)
{
  struct thread *t_a = list_entry(a, struct thread, elem);
  struct thread *t_b = list_entry(b, struct thread, elem);
  int p_a = t_a->priority > t_a->donated_priority ? t_a->priority : t_a->donated_priority;
  int p_b = t_b->priority > t_b->donated_priority ? t_b->priority : t_b->donated_priority;

  return p_a > p_b;
}

static bool 
cond_var_sort_list_func(const struct list_elem *a,
                      const struct list_elem *b,
                      void *aux UNUSED)
{
  struct condition *t_a = list_entry(a, struct condition, elem);
  struct condition *t_b = list_entry(b, struct condition, elem);
  int p_a = t_a->waiter.priority;
  int p_b = t_b->waiter.priority;

  return p_a > p_b;
}

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&sleep_list);
  list_init (&donations);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  
  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);
  /* Yield the CPU if the current thread no longer have the
     highest priority. */
  thread_preempt();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}
/*Yield the current thread if it's priority is less than the ready queue*/
void thread_preempt(void){ 
  if (!list_empty (&ready_list) && 
  (thread_current ()->priority < list_entry (list_front (&ready_list), struct thread, elem)->priority  || 
  thread_current ()->donated_priority < list_entry (list_front (&ready_list), struct thread, elem)->priority||
  thread_current ()->priority < list_entry (list_front (&ready_list), struct thread, elem)->donated_priority||
  thread_current ()->donated_priority < list_entry (list_front (&ready_list), struct thread, elem)->donated_priority))
      thread_yield ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, ready_list_less_func, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Puts the current thread to sleep until approximately TICKS timer ticks. */
void
thread_sleep (int64_t ticks)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  cur->sleep_ticks = ticks;
  old_level = intr_disable ();
  list_insert_ordered (&sleep_list, &cur->elem, sleep_list_less_func, NULL);
  thread_block ();
  intr_set_level (old_level);
}

/* Wakes all asleep threads with sleep ticks prior to TICKS timer ticks. */
void
thread_wake (int64_t ticks)
{
  struct list_elem *e;
  struct thread *t;
  enum intr_level old_level;

  while (!list_empty (&sleep_list))
    {
      e = list_front (&sleep_list);
      t = list_entry (e, struct thread, elem);
      if (t->sleep_ticks > ticks)
        break;
      old_level = intr_disable ();
      list_pop_front (&sleep_list);
      thread_unblock (t);
      intr_set_level (old_level);
    }
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, ready_list_less_func, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  enum intr_level old_level;
  struct thread *t;
  struct donation *tmp;
  struct list_elem *e;
  int p;

  old_level = intr_disable();
     int length = list_size(&donations);
     int maxi=0;
    if(length ==1) {
      e = list_begin(&donations);
      tmp = list_entry(e,struct donation,elem);
      //printf("donatd_prioti %d",tmp->donated_priority);
      if(strcmp(tmp->donee,thread_current()->name)==0){
        if(tmp->donated_priority > maxi){
          maxi = tmp->donated_priority;
          tmp->original_priority = new_priority;
        }
      }
    } else{
    for (e = list_begin (&donations); e != list_end (&donations); e = list_next (e)){
    //original_priority = list_entry(e,struct donation,elem)->original_priority;
    if((list_entry(e,struct donation,elem))->donee == thread_current()->name){
        if((list_entry(e,struct donation,elem))->donated_priority > maxi){
          maxi = list_entry(e,struct donation,elem)->donated_priority;
           tmp->original_priority = new_priority;
        }
    }}}
if(maxi>new_priority){
  thread_current()->priority = maxi;
}else {
    thread_current()->priority = new_priority;
}
 
  /* Check the priority of the thread at the front of the ready
     list and yield the CPU if the current thread no longer have
     the highest priority. */
  if (!list_empty(&ready_list))
    {
      t = list_entry (list_front (&ready_list), struct thread, elem);
      p = thread_get_priority();
      if (p < t->priority || p < t->donated_priority)
        thread_yield();
    }
  intr_set_level(old_level);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  struct thread *cur = thread_current ();

  /* If the thread's donated priority is higher, we want to
     return that instead of the original priority. */
  if (cur->priority < cur->donated_priority)
    return cur->donated_priority;

  return cur->priority;
}

void wakeup_next_waiting(struct semaphore1 *sema){
  if (!list_empty (&sema->waiters)) {
    struct thread *t = list_entry(list_pop_front(&sema->waiters),struct thread,elem);
    thread_unblock(t);
  }
}

void check_for_nest(struct lock *lock){
   struct list_elem *e; 
   struct thread *main_thread = lock->holder;
   check_for_donation(lock);
    for (e = list_begin (&donations); e != list_end (&donations); e = list_next (e)){
       if(strcmp(list_entry(e,struct donation,elem)->donor,main_thread->name)==0){
         struct lock *nest_lock = list_entry(e,struct donation,elem)->lock;
         check_for_donation(nest_lock);
       }
    }
}

void check_for_donation(struct lock *lock){
   struct thread *main_thread = lock->holder;
 //   list_entry (list_front (&ready_list), struct thread, elem);
    if(thread_current()->priority > main_thread->priority){
        struct donation *t;
        t = palloc_get_page (PAL_ZERO);
        t->lock = lock;
        strlcpy (t->donor, thread_current()->name, sizeof thread_current()->name);
        strlcpy (t->donee, main_thread->name, sizeof main_thread->name);
        t->original_priority = main_thread->original_priority;
        t->previous_priority = main_thread->priority;
        t->donated_priority = thread_current()->priority;
        //list_insert_ordered (&main_thread->donations, &t->elem, ready_list_less_func, NULL);
        list_push_front(&donations,&t->elem);
       // printf("list size %d",list_size(&main_thread->donations));
       // main_thread->previous_priority = main_thread->donated_priority!=0 ? main_thread->donated_priority: 0;
    	  main_thread->priority = thread_current()->priority ;
    }
}
/*
int findByLock(struct list *donation_list,struct lock *lock){
      int length = list_size(donation_list);
      int min_priority = 100;
      int max_same_lock = 0;
      if(length == 1){
        if(list_entry(list_begin(donation_list),struct donation,elem)->lock == lock && list_entry(e,struct donation,elem)->lock->holder->priority == thread_current()->priority){
          min_priority =list_entry(list_begin(donation_list),struct donation,elem)->previous_priority;
        }
        list_remove(list_begin(donation_list));
      }else{
        for (e = list_begin (donation_list); e != list_end (donation_list); e = list_next (e)){
          if(list_entry(e,struct donation,elem)->lock == lock && list_entry(e,struct donation,elem)->lock->holder->priority == thread_current()->priority ){
              min_priority = list_entry(e,struct donation,elem)->previous_priority;
            list_remove(e);
          }
        }
      }
      if(min_priority==100){
        return 0;
      }else {
        return min_priority;
      }
}
*/

int findByLock(struct list *donation_list, struct lock *lock ){
    struct list_elem *e;
    int min_priority =100;
    int max_same_lock =0;
    int max_priority = 0;
    int original_priority = 0;
    int count =0;
    int maximum_prio = 0;
    int length = list_size(donation_list);
    if(length ==1) {
      if(list_entry(list_begin(donation_list),struct donation,elem)->lock == lock){
        min_priority =list_entry(list_begin(donation_list),struct donation,elem)->original_priority;
        list_remove(list_begin(donation_list));
      }
    } else{
 for (e = list_begin (donation_list); e != list_end (donation_list); e = list_next (e)){
    //original_priority = list_entry(e,struct donation,elem)->original_priority;
    if((list_entry(e,struct donation,elem))->donee == thread_current()->name){
        count++;
        if((list_entry(e,struct donation,elem))->donated_priority > maximum_prio){
          maximum_prio = list_entry(e,struct donation,elem)->donated_priority;
        }
    }
    if (list_entry(e,struct donation,elem)->lock == lock){
        if(e == list_begin(donation_list)){
          min_priority = list_entry(e,struct donation,elem)->previous_priority;
          max_same_lock =list_entry(e,struct donation,elem)->donated_priority;
        }else{
          if(list_entry(e,struct donation,elem)->previous_priority < min_priority){
             min_priority = list_entry(e,struct donation,elem)->previous_priority;    
             max_same_lock =list_entry(e,struct donation,elem)->donated_priority;
          }
        } 
      list_remove(e);
    } else {
        if(e == list_begin(donation_list)){
          max_priority = list_entry(e,struct donation,elem)->donated_priority;
        }else{
          if(list_entry(e,struct donation,elem)->donated_priority > max_priority)
            max_priority = list_entry(e,struct donation,elem)->donated_priority;
        }
    }
  }
    }
 /*
  if(max_priority > max_same_lock){
    int length2  = list_size(donation_list);
    if(length2 == 1){
        if (list_entry(list_begin (donation_list),struct donation,elem)->donated_priority == max_priority){
            list_entry(list_begin (donation_list),struct donation,elem)->previous_priority = min_priority;
        }
    }else {
      for (e = list_begin (donation_list); e != list_end (donation_list); e = list_next (e)){
        if (list_entry(e,struct donation,elem)->donated_priority == max_priority){
            list_entry(e,struct donation,elem)->previous_priority = min_priority;
        }
      } 
    }
  }
  */
  if(count>1){
    return maximum_prio;
  }
   if(min_priority==100 || (length>1  && max_same_lock < max_priority) ){
      return 0;
   }else {
      return min_priority;
   }
}


void revert_donation(struct lock *lock){
  if(list_size(&donations)>0){
  int previous_priority = findByLock(&donations,lock);
     if(previous_priority != 0){
        thread_current()->priority = previous_priority;
      }
  }
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  //list_init(&t -> donations);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->original_priority = priority;
  t->donated_priority = 0;
  t->magic = THREAD_MAGIC;
  
  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
