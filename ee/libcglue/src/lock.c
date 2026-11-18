/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

/**
 * @file
 * The lock API functions required by newlib.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <reent.h>
#include <sys/lock.h>
#include <kernel.h>

// Structure representing the lock
struct __lock {
    int32_t sem_id;
    int32_t thread_id;
    int32_t count;
    int32_t waiters;
};

/* Recursive locks (stdio, malloc, atexit, env) are taken for every getc, putc,
 * malloc and free. They keep their state in the struct: thread_id is the owner,
 * LOCK_FREE when nobody holds the lock and LOCK_HANDOFF while a released lock is
 * passed on to a waiting thread; count is the nesting depth; waiters is the
 * number of threads blocked on the semaphore. The semaphore starts at 0 and
 * only wakes waiters, so taking and releasing a free lock needs no WaitSema()
 * and no SignalSema().
 *
 * The state is only changed with interrupts disabled. The EE has a single core
 * and switches threads only on interrupts and system calls, so each change is
 * atomic. */
#define LOCK_FREE    (-1)
#define LOCK_HANDOFF (-2)

#ifdef F___lock___sfp_recursive_mutex
struct __lock __lock___sfp_recursive_mutex;
#endif

#ifdef F___lock___atexit_recursive_mutex
struct __lock __lock___atexit_recursive_mutex;
#endif

#ifdef F___lock___at_quick_exit_mutex
struct __lock __lock___at_quick_exit_mutex;
#endif

#ifdef F___lock___malloc_recursive_mutex
struct __lock __lock___malloc_recursive_mutex;
#endif

#ifdef F___lock___env_recursive_mutex
struct __lock __lock___env_recursive_mutex;
#endif

#ifdef F___lock___tz_mutex
struct __lock __lock___tz_mutex;
#endif

#ifdef F___lock___dd_hash_mutex
struct __lock __lock___dd_hash_mutex;
#endif

#ifdef F___lock___arc4random_mutex
struct __lock __lock___arc4random_mutex;
#endif

static inline void __common_lock_init(_LOCK_T lock)
{
    ee_sema_t sema;
    sema.init_count = 1;
    sema.max_count  = 1;
    sema.option     = 0;
    sema.attr       = 0;
    sema.wait_threads = 0;
    lock->sem_id = CreateSema(&sema);
    lock->count = -1;
    lock->thread_id = -1;
}

static inline void __common_lock_init_recursive(_LOCK_T lock)
{
    ee_sema_t sema;
    sema.init_count = 0; /* only used to wake waiters, see LOCK_HANDOFF */
    sema.max_count  = 1;
    sema.option     = 0;
    sema.attr       = 0;
    sema.wait_threads = 0;
    lock->sem_id = CreateSema(&sema);
    lock->count = 0;
    lock->thread_id = LOCK_FREE;
    lock->waiters = 0;
}

static inline void __common_lock_close(_LOCK_T lock)
{
    DeleteSema(lock->sem_id);
}

static inline void __common_lock_close_recursive(_LOCK_T lock)
{
    DeleteSema(lock->sem_id);
}

#ifdef F___retarget_lock_init
void __retarget_lock_init(_LOCK_T *lock)
{
    _LOCK_T new_lock = (_LOCK_T)malloc(sizeof(struct __lock));
    __common_lock_init(new_lock);
    *lock = new_lock;
}
#endif

#ifdef F___retarget_lock_init_recursive
void __retarget_lock_init_recursive(_LOCK_T *lock)
{
    _LOCK_T new_lock = (_LOCK_T)malloc(sizeof(struct __lock));
    __common_lock_init_recursive(new_lock);
    *lock = new_lock;
}
#endif

#ifdef F___retarget_lock_close
void __retarget_lock_close(_LOCK_T lock)
{
	__common_lock_close(lock);
    free(lock);
}
#endif

#ifdef F___retarget_lock_close_recursive
void __retarget_lock_close_recursive(_LOCK_T lock)
{
    __common_lock_close_recursive(lock);
    free(lock);
}
#endif

#ifdef F___retarget_lock_acquire
void __retarget_lock_acquire(_LOCK_T lock)
{
	WaitSema(lock->sem_id);
}
#endif

#ifdef F___retarget_lock_acquire_recursive
void __retarget_lock_acquire_recursive(_LOCK_T lock)
{
    int32_t thread_id = GetThreadId();
    int intr = DIntr();

    if (lock->thread_id == thread_id) {
        /* This thread already holds the lock. */
        lock->count++;
    } else if (lock->thread_id == LOCK_FREE) {
        lock->thread_id = thread_id;
        lock->count = 1;
    } else {
        /* Held by another thread, or being handed to a woken waiter: wait.
         * The releasing thread sets thread_id to LOCK_HANDOFF and signals the
         * semaphore once for each waiter it wakes, so a signal that comes
         * between EIntr() and WaitSema() is not lost. */
        lock->waiters++;
        if (intr)
            EIntr();
        WaitSema(lock->sem_id);
        intr = DIntr();
        lock->thread_id = thread_id;
        lock->count = 1;
    }
    if (intr)
        EIntr();
}
#endif

#ifdef F___retarget_lock_try_acquire
int __retarget_lock_try_acquire(_LOCK_T lock)
{
	return PollSema(lock->sem_id) > 0 ? 0 : 1;
}
#endif

#ifdef F___retarget_lock_try_acquire_recursive
int __retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
    int32_t thread_id = GetThreadId();
    int intr = DIntr();
    int busy = 0;

    if (lock->thread_id == thread_id) {
        lock->count++;
    } else if (lock->thread_id == LOCK_FREE) {
        lock->thread_id = thread_id;
        lock->count = 1;
    } else {
        busy = 1; /* held by another thread or being handed over */
    }
    if (intr)
        EIntr();
    return busy;
}
#endif

#ifdef F___retarget_lock_release
void __retarget_lock_release(_LOCK_T lock)
{
	SignalSema(lock->sem_id);
}
#endif

#ifdef F___retarget_lock_release_recursive
void __retarget_lock_release_recursive(_LOCK_T lock)
{
    int32_t thread_id = GetThreadId();
    int intr, wake = 0;

    if (lock->thread_id != thread_id) {
        // error this shouldn't never happen
        perror("Error: Trying to release a lock that was not acquired by the current thread");
        exit(1);
    }

    intr = DIntr();
    if (--lock->count == 0) {
        if (lock->waiters > 0) {
            /* Hand the lock to one waiter. Until it runs, thread_id stays
             * LOCK_HANDOFF, so no other thread can take the lock first. */
            lock->waiters--;
            lock->thread_id = LOCK_HANDOFF;
            wake = 1;
        } else {
            lock->thread_id = LOCK_FREE;
        }
    }
    if (intr)
        EIntr();
    if (wake)
        SignalSema(lock->sem_id);
}
#endif

#ifdef F___locks_init
extern struct __lock __lock___malloc_recursive_mutex;
extern struct __lock __lock___atexit_recursive_mutex;
extern struct __lock __lock___at_quick_exit_mutex;
extern struct __lock __lock___sfp_recursive_mutex;
extern struct __lock __lock___env_recursive_mutex;
extern struct __lock __lock___tz_mutex;
extern struct __lock __lock___dd_hash_mutex;
extern struct __lock __lock___arc4random_mutex;

/* libcglue is built once, against the full newlib headers, but it is also
 * linked with newlib-nano (libc_nano). That one is built with _REENT_SMALL,
 * where struct _reent has a different layout and __cleanup sits at a smaller
 * offset. This mirrors the start of the _REENT_SMALL layout from
 * <sys/reent.h>, up to __cleanup. */
struct __reent_small_head {
    int _errno;
    void *_stdin, *_stdout, *_stderr;
    int _inc;
    void *_emergency;
#ifdef _REENT_BACKWARD_BINARY_COMPAT
    int _reserved_0;
    int _reserved_1;
#endif
    void *_locale;
    void *_mp;
    void (*__cleanup)(struct _reent *);
};

#define __REENT_SMALL_CLEANUP_OFFSET offsetof(struct __reent_small_head, __cleanup)

/* In the full layout, that slot lies inside the _emergency scratch buffer,
 * so clearing it there does no harm. In the small layout, the full-layout
 * __cleanup slot is _localtime_buf, a lazily allocated pointer that is NULL
 * at a first start and points into the old heap after a restart. */
_Static_assert(__REENT_SMALL_CLEANUP_OFFSET >= offsetof(struct _reent, _emergency) &&
                   __REENT_SMALL_CLEANUP_OFFSET + sizeof(void *) <=
                       offsetof(struct _reent, _emergency) + sizeof(((struct _reent *)0)->_emergency),
               "the _REENT_SMALL __cleanup slot must lie inside _emergency of the full layout");

void __locks_init()
{
    /* Reset stdio initialization state so __sinit will reinitialize
     * FILE structs and their locks on next use.
     * _impure_data lives in .data (survives BSS clear on restart via
     * ExecPS2), while FILE structs and locks live in .bss (zeroed).
     * Without this, __sinit skips reinitialization and uses NULL locks. */
    _REENT_CLEANUP(_REENT) = NULL;
    /* The same for newlib-nano, see struct __reent_small_head above. */
    memset((char *)_REENT + __REENT_SMALL_CLEANUP_OFFSET, 0, sizeof(void *));

    _LOCK_T lock_malloc = &__lock___malloc_recursive_mutex;
    _LOCK_T lock_atexit = &__lock___atexit_recursive_mutex;
    _LOCK_T lock_quick_exit = &__lock___at_quick_exit_mutex;
    _LOCK_T lock_sfp = &__lock___sfp_recursive_mutex;
    _LOCK_T lock_env = &__lock___env_recursive_mutex;
    _LOCK_T lock_tz = &__lock___tz_mutex;
    _LOCK_T lock_dd_hash = &__lock___dd_hash_mutex;
    _LOCK_T lock_arc4random = &__lock___arc4random_mutex;
    
    __common_lock_init_recursive(lock_malloc);
    __common_lock_init_recursive(lock_atexit);
    __common_lock_init(lock_quick_exit);
    __common_lock_init_recursive(lock_sfp);
    __common_lock_init_recursive(lock_env);
    __common_lock_init(lock_tz);
    __common_lock_init(lock_dd_hash);
    __common_lock_init(lock_arc4random);
}
#endif

#ifdef F___locks_deinit
extern struct __lock __lock___malloc_recursive_mutex;
extern struct __lock __lock___atexit_recursive_mutex;
extern struct __lock __lock___at_quick_exit_mutex;
extern struct __lock __lock___sfp_recursive_mutex;
extern struct __lock __lock___env_recursive_mutex;
extern struct __lock __lock___tz_mutex;
extern struct __lock __lock___dd_hash_mutex;
extern struct __lock __lock___arc4random_mutex;

void __locks_deinit()
{
    _LOCK_T lock_malloc = &__lock___malloc_recursive_mutex;
    _LOCK_T lock_atexit = &__lock___atexit_recursive_mutex;
    _LOCK_T lock_quick_exit = &__lock___at_quick_exit_mutex;
    _LOCK_T lock_sfp = &__lock___sfp_recursive_mutex;
    _LOCK_T lock_env = &__lock___env_recursive_mutex;
    _LOCK_T lock_tz = &__lock___tz_mutex;
    _LOCK_T lock_dd_hash = &__lock___dd_hash_mutex;
    _LOCK_T lock_arc4random = &__lock___arc4random_mutex;
    

    __common_lock_close_recursive(lock_malloc);
    __common_lock_close_recursive(lock_atexit);
    __common_lock_close(lock_quick_exit);
    __common_lock_close_recursive(lock_sfp);
    __common_lock_close_recursive(lock_env);
    __common_lock_close(lock_tz);
    __common_lock_close(lock_dd_hash);
    __common_lock_close(lock_arc4random);
}
#endif