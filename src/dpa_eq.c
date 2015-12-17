/* A libfabric provider for the A3CUBE Ronnie network.
 *
 * (C) Copyright 2015 - University of Torino, Italy
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This work is a part of Paolo Inaudi's MSc thesis at Computer Science
 * Department of University of Torino, under the supervision of Prof.
 * Marco Aldinucci. This is work has been made possible thanks to
 * the Memorandum of Understanding (2014) between University of Torino and 
 * A3CUBE Inc. that established a joint research lab at
 * Computer Science Department of University of Torino, Italy.
 *
 * Author: Paolo Inaudi <p91paul@gmail.com>  
 *       
 * Contributors: 
 * 
 *     Emilio Billi (A3Cube Inc. CSO): hardware and DPAlib support
 *     Paola Pisano (UniTO-A3Cube CEO): testing environment
 *     Marco Aldinucci (UniTO-A3Cube CSO): code design supervision"
 */
#define LOG_SUBSYS FI_LOG_EQ
#include "dpa_eq.h"

static int dpa_eq_close(struct fid* fid);
struct fi_ops dpa_fid_eq_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_eq_close,
  .bind = fi_no_bind,
  .control = fi_no_control,
  .ops_open = fi_no_ops_open
};

static ssize_t dpa_eq_read(struct fid_eq *eq, uint32_t *event,
                           void *buf, size_t len, uint64_t flags);
static ssize_t dpa_eq_readerr(struct fid_eq *eq, struct fi_eq_err_entry *buf,
                              uint64_t flags);
static ssize_t dpa_eq_sread(struct fid_eq *eq, uint32_t *event,
                            void *buf, size_t len, int timeout, uint64_t flags);
static ssize_t dpa_eq_write(struct fid_eq *eq, uint32_t event,
                            const void *buf, size_t len, uint64_t flags);
static const char* dpa_eq_strerror(struct fid_eq *eq, int prov_errno,
                                   const void *err_data, char *buf, size_t len);

struct fi_ops_eq dpa_eq_ops = {
  .size = sizeof(struct fi_ops_eq),
  .read = dpa_eq_read,
  .readerr = dpa_eq_readerr,
  .sread = dpa_eq_sread,
  .write = dpa_eq_write,
  .strerror = dpa_eq_strerror
};

int	dpa_eq_open(struct fid_fabric *fabric, struct fi_eq_attr *attr,
                struct fid_eq **eq, void *context) {  
  if (!attr) return -FI_ENODATA;
  
  DPA_DEBUG("Checking wait object required\n");
  switch (attr->wait_obj) {
  case FI_WAIT_NONE:
  case FI_WAIT_UNSPEC:
    break;
  default:
    VERIFY_FAIL(attr->wait_obj, FI_WAIT_NONE);
  };

  DPA_DEBUG("Building event queue\n");
  dpa_fid_eq* eq_priv = ALLOC_INIT(dpa_fid_eq, {
      .eq = {
        .fid = {
          .fclass = FI_CLASS_EQ,
          .context = context,
          .ops = &dpa_fid_eq_ops,
        },
        .ops = &dpa_eq_ops
      },
      .fabric = fabric,
    });

  queue_progress_init(&eq_priv->progress);
  slist_init(&eq_priv->event_queue);
  slist_init(&eq_priv->error_queue);
  slist_init(&eq_priv->free_list);
  fastlock_cond_init(&eq_priv->cond);

  for (int i = 0; i < attr->size; i++) {
    struct dpa_eq_event *event = calloc(1, sizeof(struct dpa_eq_event));
    if (!event) {
      DPA_WARN("Out of memory\n");
      return -FI_ENOMEM;
    }
    slist_insert_tail_unsafe(&event->list_entry, &eq_priv->free_list);
  }
  
  *eq = &(eq_priv->eq);
  return 0;
}

static void free_eq(struct slist* eq){
  slist_destroy(eq, dpa_eq_event, list_entry, no_destroyer);
}

static int dpa_eq_close(struct fid* fid) {
  DPA_DEBUG("Closing event queue\n");
  dpa_fid_eq* eq_priv = container_of(fid, dpa_fid_eq, eq.fid);
  free_eq(&eq_priv->event_queue);
  free_eq(&eq_priv->error_queue);
  free_eq(&eq_priv->free_list);
  free(eq_priv);
}

static inline dpa_eq_event* get_head(slist* event_queue, uint8_t peek) {
  slist_entry* entry = peek ? event_queue->head : slist_remove_head(event_queue);
  return entry ? container_of(entry, dpa_eq_event, list_entry) : NULL;
}

static inline ssize_t eq_read_priv(dpa_fid_eq* eq, slist* event_queue,
                                   void* buf, size_t len, uint32_t* event,
                                   uint64_t flags) {
  uint8_t peek = flags & FI_PEEK;
  dpa_eq_event* head = get_head(event_queue, peek);
  if (!head) return -FI_EAGAIN;
  ssize_t copy_size = MIN(head->entry_size, len);
  memcpy(buf, &head->entry, copy_size);
  if (event)
    *event = head->event;
  if (peek)
    slist_insert_tail(&head->list_entry, &eq->free_list);
  return copy_size;
}

static inline ssize_t dpa_eq_read(struct fid_eq *eq, uint32_t *event,
                                  void *buf, size_t len, uint64_t flags){
  return dpa_eq_sread(eq, event, buf, len, 0, flags);
}

static ssize_t dpa_eq_readerr(struct fid_eq *eq, struct fi_eq_err_entry *buf,
                              uint64_t flags){
  DPA_DEBUG("Reading from event error queue\n");
  dpa_fid_eq* eq_priv = container_of(eq, dpa_fid_eq, eq);
  size_t size = sizeof(struct fi_eq_err_entry);
  return eq_read_priv(eq_priv, &eq_priv->error_queue, buf, size, NULL, flags);
}
  
static ssize_t dpa_eq_sread(struct fid_eq *eq, uint32_t *event,
                            void *buf, size_t len, int timeout, uint64_t flags) {
  dpa_fid_eq* eq_priv = container_of(eq, dpa_fid_eq, eq);
  ssize_t result = 0;
  do {
    make_queue_progress(&eq_priv->progress, 0);
    if (!slist_empty(&eq_priv->error_queue)) {
      DPA_DEBUG("Error queue not empty\n");
      return -FI_EAVAIL;
    }
    result = eq_read_priv(eq_priv, &eq_priv->event_queue, buf, len, event, flags);
    if (result == -FI_EAGAIN) {
      make_queue_progress(&eq_priv->progress, timeout);
      // with automatic progress wait until progress happens
      if (!eq_priv->progress && timeout) {
        LIST_SAFE(&eq_priv->event_queue, ({
              fastlock_wait_timeout(&eq_priv->cond, &eq_priv->event_queue.lock, timeout);
            }));
      }
    }
  } while(timeout > 0);
  return result;
}
 
static inline dpa_eq_event* get_empty_event(dpa_fid_eq* eq) {
  slist_entry* entry = slist_remove_head(&eq->free_list);
  return entry
    ? container_of(entry, dpa_eq_event, list_entry)
    : calloc(1, sizeof(dpa_eq_event));
}

static inline int eq_signal(dpa_fid_eq* eq) {
  // if progress is manual nobody ever waits
  if (eq->progress.func) return 0;
  return fastlock_signal(&eq->cond);
}

ssize_t eq_add(dpa_fid_eq* eq, uint32_t event_num, const void* buf, size_t len, uint64_t flags, int error) {
  if (!eq) return 0;
  dpa_eq_event* event = get_empty_event(eq);
  size_t copy_size = MIN(len, sizeof(event->entry));
  memcpy(&event->entry, buf, copy_size);
  event->event = event_num;
  event->error = error;
  event->entry_size = copy_size;
  slist_insert_tail(&event->list_entry, error ? &(eq->error_queue) : &(eq->event_queue));
  eq_signal(eq);
  return copy_size;
}
  
static ssize_t dpa_eq_write(struct fid_eq *eq, uint32_t event,
                            const void *buf, size_t len, uint64_t flags) {
  DPA_DEBUG("Adding item to event queue\n");
  dpa_fid_eq* eq_priv = container_of(eq, dpa_fid_eq, eq);
  return eq_add(eq_priv, event, buf, len, flags, 0);
}
  
static const char* dpa_eq_strerror(struct fid_eq *eq, int prov_errno,
                                   const void *err_data, char *buf, size_t len) {
  return "";
}
