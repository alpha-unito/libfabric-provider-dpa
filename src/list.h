/*
 * Copyright (c) 2011 Intel Corporation.  All rights reserved.
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
 *
 */
typedef struct slist slist;
typedef struct slist_entry slist_entry;
typedef struct dlist dlist;
typedef struct dlist_entry dlist_entry;
#if !defined(LIST_H)
#define LIST_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <errno.h>
#include <rdma/fabric.h>

#include "locks.h"

#define LIST_SAFE(list, op) THREADSAFE(&(list)->lock, op)
typedef void (*element_destroyer)(void* element);

/*
 * Double-linked list
 */
struct dlist_entry {
  struct dlist_entry	*next;
  struct dlist_entry	*prev;
};

struct dlist {
  dlist_entry list;
  fastlock_t lock;
};

static inline void dlist_init_unsafe(dlist_entry *list)
{
  list->next = list;
  list->prev = list;
}

static inline void dlist_init(dlist *list){
  fastlock_init(&list->lock);
  LIST_SAFE(list, dlist_init_unsafe(&list->list));
}

static inline void no_destroyer(void* element){};

#define dlist_destroy(list, type, field, destroyer) do {                \
    for (dlist_entry* _e = (list)->list.next; !_e; _e = _e->next) {     \
      void *element = container_of(_e, type, field);                    \
      destroyer(element);                                             \
      free(element);                                                    \
    }                                                                   \
    fastlock_destroy(&(list)->lock);                                    \
  } while (0)      

static inline int dlist_empty(dlist_entry *list)
{
  return list->next == list;
}

static inline void dlist_insert_after_unsafe(dlist_entry *item, dlist_entry *entry)  
{
  item->next = entry->next;
  item->prev = entry;
  entry->next->prev = item;
  entry->next = item;
}

static inline void dlist_insert_after(dlist_entry *item, dlist_entry *entry, dlist *list){
  LIST_SAFE(list, dlist_insert_after_unsafe(item, entry));
}

static inline void dlist_insert_before_unsafe(dlist_entry *item, dlist_entry *entry){
  dlist_insert_after_unsafe(item, entry->prev);
}

static inline void dlist_insert_before(dlist_entry *item, dlist_entry *entry, dlist *list){
  dlist_insert_after(item, entry->prev, list);
}

#define dlist_insert_head dlist_insert_after
#define dlist_insert_tail dlist_insert_before

static inline void dlist_remove_unsafe(dlist_entry *item){
  item->prev->next = item->next;
  item->next->prev = item->prev;
}

static inline void dlist_remove(dlist_entry *item, dlist* list){
  LIST_SAFE(list, dlist_remove_unsafe(item));
}

typedef int dlist_match_func_t(dlist_entry *item, const void *arg);

static inline dlist_entry *dlist_remove_first_match_unsafe(dlist_entry *start, dlist_match_func_t *match,
                                                    const void *arg)
{
  dlist_entry *item;

  for (item = start->next; item != start; item = item->next) {
    if (match(item, arg)) {
      dlist_remove_unsafe(item);
      return item;
    }
  }

  return NULL;
}

static inline dlist_entry *dlist_remove_first_match(dlist_entry *start, dlist_match_func_t *match,
                                                    const void *arg, dlist* list){
  dlist_entry* result;
  LIST_SAFE(list, result = dlist_remove_first_match_unsafe(start, match, arg));
  return result;
}

/*
 * Single-linked list
 */
struct slist_entry {
  struct slist_entry	*next;
};

struct slist {
  slist_entry	*head;
  slist_entry	*tail;
  fastlock_t lock;
};
  
static inline void slist_init_unsafe(slist *list)
{
  list->head = list->tail = NULL;
}

static inline void slist_init(slist *list) {
  fastlock_init(&list->lock);
  LIST_SAFE(list, slist_init_unsafe(list));
}

#define slist_destroy(list, type, field, destroyer) do {                \
    void* prev = NULL, *element;                                        \
    for (slist_entry* _e = (list)->head; _e; _e = _e->next) {           \
      element = container_of(_e, type, field);                          \
      destroyer(element);                                               \
      if(prev) free(prev);                                              \
      prev = element;                                                   \
    }                                                                   \
    free(prev);                                                         \
    fastlock_destroy(&(list)->lock);                                    \
  } while (0)                                                           

static inline int slist_empty(slist *list) {
  return !list->head;
}

static inline void slist_lock(slist *list) {
  fastlock_acquire(&list->lock);
}

static inline void slist_unlock(slist* list) {
  fastlock_release(&list->lock);
}

static inline void slist_insert_head_unsafe(slist_entry *item, slist *list) {
  if (slist_empty(list)){
    list->tail = item;
    item->next = NULL;
  }
  else
    item->next = list->head;
  
  list->head = item;
}

static inline void slist_insert_head(slist_entry *item, slist *list) {
  LIST_SAFE(list, slist_insert_head_unsafe(item,list));
}

static inline void slist_insert_tail_unsafe(slist_entry *item, slist *list) {
  if (slist_empty(list))
    list->head = item;
  else
    list->tail->next = item;
  item->next = NULL;
  list->tail = item;
}

static inline void slist_insert_tail(slist_entry *item, slist *list) {
  LIST_SAFE(list, slist_insert_tail_unsafe(item, list));
}
  
static inline slist_entry *slist_remove_head_unsafe(slist *list) {
  slist_entry *item;

  item = list->head;
  if (list->head == list->tail)
    slist_init_unsafe(list);
  else
    list->head = item->next;
  return item;
}

static inline slist_entry *slist_remove_head(slist *list) {
  slist_entry* result;
  LIST_SAFE(list, result = slist_remove_head_unsafe(list));
  return result;
}

typedef int slist_match_func_t(slist_entry *item, const void *arg);

static inline slist_entry *slist_remove_first_match_unsafe(slist *list, slist_match_func_t *match, const void *arg) {
  slist_entry *item, *prev;

  for (prev = NULL, item = list->head; item; prev = item, item = item->next) {
    if (match(item, arg)) {
      if (prev)
        prev->next = item->next;
      else
        list->head = item->next;

      if (!item->next)
        list->tail = prev;

      return item;
    }
  }

  return NULL;
}

static inline slist_entry *slist_remove_first_match(slist *list, slist_match_func_t *match, const void *arg) {
  slist_entry* result;
  LIST_SAFE(list, result = slist_remove_first_match_unsafe(list, match, arg));
  return result;
}


#endif /* LIST_H */
