/*
 * Copyright (c) 2013-2014 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LOCKS_H
#define LOCKS_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>


#ifdef HAVE_ATOMICS
#  include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if PT_LOCK_SPIN == 1

  typedef struct spinlock_cond {
    uint64_t locked;
    uint64_t released;
    pthread_spinlock_t lock;
  } spinlock_cond;

#define fastlock_t_ pthread_spinlock_t
#define fastlock_cond_t_ spinlock_cond
#define fastlock_init_(lock) pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE)
#define fastlock_destroy_(lock) pthread_spin_destroy(lock)
#define fastlock_acquire_(lock) pthread_spin_lock(lock)
#define fastlock_release_(lock) pthread_spin_unlock(lock)

  static inline int spin_wait(fastlock_cond_t_* cond, pthread_spinlock_t* lock) {
    uint64_t wait = cond->locked;
    cond->locked++;
    fastlock_release_(lock);
    while (cond->released <= wait);
    fastlock_acquire_(lock);
    return 0;
  }
  static inline int spin_signal(fastlock_cond_t_* cond) {
    fastlock_acquire_(&cond->lock);
    cond->released++;
    fastlock_release_(&cond->lock);
    return 0;
  }
  static inline int spin_signal_all(fastlock_cond_t_* cond) {
    fastlock_acquire_(&cond->lock);
    while (cond->released < cond->locked)
      cond->released++;
    fastlock_release_(&cond->lock);
    return 0;
  }
    
#define fastlock_cond_init_(cond) do { (cond)->locked = (cond)->released = 0; fastlock_init_(&(cond)->lock); } while (0)
#define fastlock_cond_destroy_(cond) slist_destroy(cond, cond_entry, list_entry, NULL)
#define fastlock_wait_(cond, lock) spin_wait(cond, lock)
#define fastlock_signal_(cond) spin_signal(cond)
#define fastlock_signal_all_(cond) spin_signal_all(cond)

#else

#define fastlock_t_ pthread_mutex_t
#define fastlock_cond_t_ pthread_cond_t
#define fastlock_init_(lock) pthread_mutex_init(lock, NULL)
#define fastlock_destroy_(lock) pthread_mutex_destroy(lock)
#define fastlock_acquire_(lock) pthread_mutex_lock(lock)
#define fastlock_release_(lock) pthread_mutex_unlock(lock)

#define fastlock_cond_init_(cond) pthread_cond_init(cond, NULL)
#define fastlock_cond_destroy_(cond) pthread_cond_destroy(cond)
#define fastlock_wait_(cond, lock) pthread_cond_wait(cond, lock)
#define fastlock_signal_(cond) pthread_cond_signal(cond)
#define fastlock_signal_all_(cond) pthread_cond_broadcast(cond)

#endif /* PT_LOCK_SPIN */

#define fastlock_t fastlock_t_
#define fastlock_cond_t fastlock_cond_t_
#define fastlock_init(lock) fastlock_init_(lock)
#define fastlock_destroy(lock) fastlock_destroy_(lock)
#define fastlock_acquire(lock) fastlock_acquire_(lock)
#define fastlock_release(lock) fastlock_release_(lock)

#define fastlock_cond_init(cond) fastlock_cond_init_(cond)
#define fastlock_cond_destroy(cond) fastlock_cond_destroy_(cond)
#define fastlock_wait(cond, lock) fastlock_wait_(cond, lock)
#define fastlock_wait_timeout(cond, lock, timeout) fastlock_wait_(cond, lock)
#define fastlock_signal(cond) fastlock_signal_(cond)
#define fastlock_signal_all(cond) fastlock_signal_all_(cond)

#if ENABLE_DEBUG
#define ATOMIC_IS_INITIALIZED(atomic) assert(atomic->is_initialized)
#else
#define ATOMIC_IS_INITIALIZED(atomic)
#endif

#ifdef HAVE_ATOMICS
  typedef struct {
    atomic_int val;
#if ENABLE_DEBUG
    int is_initialized;
#endif
  } atomic_t;

  static inline int atomic_inc(atomic_t *atomic)
  {
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_fetch_add_explicit(&atomic->val, 1, memory_order_acq_rel) + 1;
  }

  static inline int atomic_dec(atomic_t *atomic)
  {
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_fetch_sub_explicit(&atomic->val, 1, memory_order_acq_rel) - 1;
  }

  static inline int atomic_set(atomic_t *atomic, int value)
  {
	ATOMIC_IS_INITIALIZED(atomic);
	atomic_store(&atomic->val, value);
	return value;
  }

  static inline int atomic_get(atomic_t *atomic)
  {
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_load(&atomic->val);
  }

  /* avoid using "atomic_init" so we don't conflict with symbol/macro from stdatomic.h */
  static inline void atomic_initialize(atomic_t *atomic, int value)
  {
	atomic_init(&atomic->val, value);
#if ENABLE_DEBUG
	atomic->is_initialized = 1;
#endif
  }

#else

  typedef struct {
	fastlock_t lock;
	int val;
#if ENABLE_DEBUG
	int is_initialized;
#endif
  } atomic_t;

  static inline int atomic_inc(atomic_t *atomic)
  {
	int v;

	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	v = ++(atomic->val);
	fastlock_release(&atomic->lock);
	return v;
  }

  static inline int atomic_dec(atomic_t *atomic)
  {
	int v;

	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	v = --(atomic->val);
	fastlock_release(&atomic->lock);
	return v;
  }

  static inline int atomic_set(atomic_t *atomic, int value)
  {
	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	atomic->val = value;
	fastlock_release(&atomic->lock);
	return value;
  }

  /* avoid using "atomic_init" so we don't conflict with symbol/macro from stdatomic.h */
  static inline void atomic_initialize(atomic_t *atomic, int value)
  {
	fastlock_init(&atomic->lock);
	atomic->val = value;
#if ENABLE_DEBUG
	atomic->is_initialized = 1;
#endif
  }

  static inline int atomic_get(atomic_t *atomic)
  {
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic->val;
  }

#endif // HAVE_ATOMICS
  
#define THREADSAFE(lock, op) {                  \
    fastlock_acquire(lock);                     \
    op;                                         \
    fastlock_release(lock);                     \
  }

  

#ifdef __cplusplus
}
#endif

#endif
