#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "hash.h"

/* States in a thread's life cycle. */
enum thread_status
{
    THREAD_RUNNING, /* Running thread. */
    THREAD_READY,   /* Not running but ready to run. */
    THREAD_BLOCKED, /* Waiting for an event to trigger. */
    THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
static struct list lru_list;
static struct lock lru_list_lock;
static struct page *lru_clock;
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;                 /* Thread identifier. */
    enum thread_status status; /* Thread state. */
    char name[16];             /* Name (for debugging purposes). */
    uint8_t *stack;            /* Saved stack pointer. */
    int priority;              /* Priority. */
    struct list_elem allelem;  /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem; /* List element. */

    /* Owned by devices/timer.c. */
    int64_t wake_ticks; /* Ticks to wake up. */

    /* Shared between thread.c and synch.c. */
    int original_priority;   /* Original priority before donation. */
    struct list donators;    /* List of donators. */
    struct list_elem doelem; /* List element for donators list. */
    struct thread *donee;    /* Thread that is given priority. */

    struct hash vm;
    struct list mappingList;
    int map_id;
    /* Owned by thread.c. */
    int nice;       /* Figure that indicates how nice to others. */
    int recent_cpu; /* Weighted average amount of received CPU time. */

#ifdef USERPROG
    /* Shared between userprog/process.c and userprog/syscall.c. */
    uint32_t *pagedir;         /* Page directory. */
    struct process *pcb;       /* Process control block. */
    struct list children;      /* List of children processes. */
    struct list fdt;           /* List of file descriptor entries. */
    int next_fd;               /* File descriptor for next file. */
    struct file *running_file; /* Currently running file. */
#endif

    /* Owned by thread.c. */
    unsigned magic; /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func(struct thread *t, void *aux);
void thread_foreach(thread_action_func *, void *);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

struct list *get_sleep_list(void);
struct list *thread_get_donators(void);
struct thread *thread_get_donee(void);
void thread_set_donee(struct thread *);

#ifdef USERPROG
uint32_t *thread_get_pagedir(void);
void thread_set_pagedir(uint32_t *);
struct process *thread_get_pcb(void);
void thread_set_pcb(struct process *);
struct list *thread_get_children(void);
struct list *thread_get_fdt(void);
int thread_get_next_fd(void);
struct file *thread_get_running_file(void);
void thread_set_running_file(struct file *);
#endif

list_less_func less_priority;

#endif /* threads/thread.h */
