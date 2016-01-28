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
#define LOG_SUBSYS FI_LOG_CQ

#include "dpa.h"
#include "dpa_cq.h"

static int dpa_cq_close(struct fid* fid);
static int dpa_cq_wait_data(struct fid_cq* cq, uint64_t* data, uint64_t flags);
static int dpa_cq_ops_open(struct fid *fid, const char *name,
                           uint64_t flags, void **ops, void *context);
static fi_dpa_ops_cq dpa_ops_cq = {
  .size = sizeof(fi_dpa_ops_cq),
  .wait_data = dpa_cq_wait_data
};

static struct fi_ops dpa_fid_cq_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_cq_close,
  .bind = fi_no_bind,
  .control = fi_no_control,
  .ops_open = dpa_cq_ops_open
};

static ssize_t dpa_cq_read(struct fid_cq *cq, void *buf, size_t count);
static ssize_t dpa_cq_readfrom(struct fid_cq *cq, void *buf, size_t count, fi_addr_t *src_addr);
static ssize_t dpa_cq_readerr(struct fid_cq *cq, struct fi_cq_err_entry *buf, uint64_t flags);
static ssize_t dpa_cq_sread(struct fid_cq *cq, void *buf, size_t count, const void *cond, int timeout);
static ssize_t dpa_cq_sreadfrom(struct fid_cq *cq, void *buf, size_t count, fi_addr_t *src_addr, const void *cond, int timeout);
static int dpa_cq_signal(struct fid_cq *cq);
static const char * dpa_cq_strerror(struct fid_cq *cq, int prov_errno, const void *err_data, char *buf, size_t len);

struct fi_ops_cq dpa_cq_ops = {
  .size = sizeof(struct fi_ops_cq),
  .read = dpa_cq_read,
  .readfrom = dpa_cq_readfrom,
  .readerr = dpa_cq_readerr,
  .sread = dpa_cq_sread,
  .sreadfrom = dpa_cq_sreadfrom,
  .signal = dpa_cq_signal,
  .strerror = dpa_cq_strerror
};

int format_size(enum fi_cq_format format){
  switch(format){

  case FI_CQ_FORMAT_CONTEXT:
    return sizeof(struct fi_cq_entry);

  case FI_CQ_FORMAT_DATA:
    return sizeof(struct fi_cq_data_entry);

  case FI_CQ_FORMAT_TAGGED:
    return sizeof(struct fi_cq_tagged_entry);

  case FI_CQ_FORMAT_UNSPEC:
  case FI_CQ_FORMAT_MSG:
  default:
    return sizeof(struct fi_cq_msg_entry);
  }
}
    
  

int dpa_cq_open(struct fid_domain *domain, struct fi_cq_attr *attr, struct fid_cq **cq, void *context){
  if (!attr) return -FI_ENODATA;

  DPA_DEBUG("Checking wait object required\n");
  switch (attr->wait_obj) {
  case FI_WAIT_NONE:
  case FI_WAIT_UNSPEC:
    break;
  default:
    VERIFY_FAIL(attr->wait_obj, FI_WAIT_NONE);
  };
  int entry_size = format_size(attr->format);

  DPA_DEBUG("Building completion queue\n");
  dpa_fid_cq* cq_priv = ALLOC_INIT(dpa_fid_cq, {
      .cq = {
        .fid = {
          .fclass = FI_CLASS_CQ,
          .context = context,
          .ops = &dpa_fid_cq_ops,
        },
        .ops = &dpa_cq_ops
      },
      .progress = {
        .func = NULL
      },
      .interrupt = {
        .handle = NULL
      },
      .entry_size = entry_size,
      .domain = container_of(domain, dpa_fid_domain, domain),
      .wait_obj = attr->wait_obj,
  });

  queue_progress_init(&cq_priv->progress);
  queue_interrupt_init(&cq_priv->interrupt);
  slist_init(&cq_priv->event_queue);
  slist_init(&cq_priv->error_queue);
  slist_init(&cq_priv->free_list);
  fastlock_cond_init(&cq_priv->cond);

  for (int i = 0; i < attr->size; i++) {
    struct dpa_cq_event *event = calloc(1, sizeof(struct dpa_cq_event));
    if (!event) {
      DPA_WARN("Out of memory\n");
      return -FI_ENOMEM;
    }
    slist_insert_tail_unsafe(&event->list_entry, &cq_priv->free_list);
  }
  
  *cq = &(cq_priv->cq);
  return 0;
}

static void free_cq(struct slist* cq){
  slist_destroy(cq, dpa_cq_event, list_entry, no_destroyer);
}

static int dpa_cq_close(struct fid* fid) {
  DPA_DEBUG("Closing completion queue\n");
  dpa_fid_cq* cq_priv = container_of(fid, dpa_fid_cq, cq.fid);
  free_cq(&cq_priv->event_queue);
  free_cq(&cq_priv->error_queue);
  free_cq(&cq_priv->free_list);
  free(cq_priv);
}

static inline dpa_cq_event* get_empty_event(dpa_fid_cq* cq) {
  slist_entry* entry = slist_remove_head_unsafe(&cq->free_list);
  return entry
    ? container_of(entry, dpa_cq_event, list_entry)
    : calloc(1, sizeof(dpa_cq_event));
}

static inline void cq_lock(dpa_fid_cq* cq) {
  if (cq->domain->threading < FI_THREAD_COMPLETION)
    fastlock_acquire(&cq->free_list.lock);
}

static inline void cq_unlock(dpa_fid_cq* cq) {
  if (cq->domain->threading < FI_THREAD_COMPLETION)
    fastlock_release(&cq->free_list.lock);
}

static inline int cq_wait(dpa_fid_cq* cq, int timeout) {
  cq_lock(cq);
  int result = fastlock_wait_timeout(&cq->cond, &cq->free_list.lock, timeout);
  cq_unlock(cq);
  return result;
}

static inline int cq_signal(dpa_fid_cq* cq) {
  if (!(cq->progress.func || cq->interrupt.handle))
    return fastlock_signal(&cq->cond);
  else return 0;
}

void cq_add_src(dpa_fid_cq* cq, struct fi_cq_err_entry* entry, fi_addr_t src_addr) {
  DPA_DEBUG("Adding item to completion queue\n");

  cq_lock(cq);

  dpa_cq_event* event = get_empty_event(cq);
  memcpy(&(event->entry), entry, sizeof(struct fi_cq_err_entry));
  event->src_addr = src_addr;
  slist* queue = entry->err ? &(cq->error_queue) : &(cq->event_queue);
  slist_insert_tail_unsafe(&(event->list_entry), queue);

  cq_unlock(cq);

  cq_signal(cq);
}

static inline ssize_t cq_read_priv(dpa_fid_cq* cq, struct slist* event_queue,
                                   size_t entry_size, void* buf,
                                   fi_addr_t* src_addr, size_t count) {
  cq_lock(cq);

  int i;
  for(i = 0; i<count; i++) {
    slist_entry* list_entry = slist_remove_head_unsafe(event_queue);
    if (!list_entry)
      break;

    dpa_cq_event* head = container_of(list_entry, dpa_cq_event, list_entry);
    memcpy(buf, &head->entry, entry_size);
    buf += entry_size;
    if (src_addr) {
      memcpy(src_addr, &head->src_addr, sizeof(fi_addr_t));
      src_addr++;
    }
    slist_insert_tail_unsafe(&head->list_entry, &cq->free_list);
  }
  
  cq_unlock(cq);
  return i > 0 ? i : -FI_EAGAIN;
}

static ssize_t dpa_cq_read(struct fid_cq *cq, void *buf, size_t count){
  return dpa_cq_readfrom(cq, buf, count, NULL);
}

static inline ssize_t dpa_cq_readfrom(struct fid_cq *cq, void *buf, size_t count, fi_addr_t *src_addr){ 
  return dpa_cq_sreadfrom(cq, buf, count, src_addr, NULL, 0);
}
  
static ssize_t dpa_cq_readerr(struct fid_cq *cq, struct fi_cq_err_entry *buf, uint64_t flags){
  DPA_DEBUG("Reading from error queue\n");
  dpa_fid_cq* cq_priv = container_of(cq, dpa_fid_cq, cq);
  return cq_read_priv(cq_priv, &cq_priv->error_queue, sizeof(struct fi_cq_err_entry), buf, NULL, 1);
}
   
static ssize_t dpa_cq_sread(struct fid_cq *cq, void *buf, size_t count, const void *cond, int timeout){
  return dpa_cq_sreadfrom(cq, buf, count, NULL, cond, timeout);
}
static inline void make_cq_progress(dpa_fid_cq* cq, int timeout);
static inline ssize_t dpa_cq_sreadfrom(struct fid_cq *cq, void *buf, size_t count, fi_addr_t *src_addr, const void *cond, int timeout){
  dpa_fid_cq* cq_priv = container_of(cq, dpa_fid_cq, cq);

  //start with immediate progress
  make_cq_progress(cq_priv, 0);

  ssize_t result = cq_read_priv(cq_priv, &cq_priv->event_queue,
                                cq_priv->entry_size, buf, src_addr, count);
  if (result == -FI_EAGAIN && timeout) {
    DPA_DEBUG("Await completion queue progress\n");
    if (cq_priv->progress.func || cq_priv->interrupt.handle)
      make_cq_progress(cq_priv, timeout);
    else
      cq_wait(cq_priv, timeout);
  }

  return result;
}

static int dpa_cq_signal(struct fid_cq *cq){
  return cq_signal(container_of(cq, dpa_fid_cq, cq));
}

static const char* dpa_cq_strerror(struct fid_cq *cq, int prov_errno, const void *err_data,
                                   char *buf, size_t len){
  return "";
}

static int dpa_cq_wait_data(struct fid_cq* cq, uint64_t* data, uint64_t flags) {
  if (!data) return -FI_EINVAL;
  dpa_intid_t interrupt_id = (dpa_intid_t) *data;
  if (*data != interrupt_id)
    return -FI_EINVAL; // truncation occurred, cannot use this interrupt id

  queue_interrupt* interrupt = &(container_of(cq, dpa_fid_cq, cq)->interrupt);
  unsigned int dpa_int_flags = interrupt_id ? DPA_FLAG_FIXED_INTNO : NO_FLAGS;
  dpa_error_t error;
  DPA_DEBUG("Create interrupt virtual device\n");
  DPAOpen(&interrupt->sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAOpen, return -FI_EOTHER);

  DPA_DEBUG("Create completion queue interrupt\n");
  DPACreateInterrupt(interrupt->sd, &interrupt->handle, localAdapterNo,
                     &interrupt_id, NULL, NULL, dpa_int_flags, &error);
  DPALIB_CHECK_ERROR(DPACreateInterrupt, goto dpa_cq_listen_close);

  DPA_DEBUG("Created completion queue interrupt number %u\n", interrupt_id);

  interrupt->event_flags = flags;
  interrupt->id = interrupt_id;
  *data = interrupt_id;
  
  return FI_SUCCESS;

 dpa_cq_listen_close:
  DPAClose(interrupt->sd, NO_FLAGS, &error);
  return -FI_EOTHER;
}
  
static inline void wait_cq_interrupt(dpa_fid_cq* cq, int timeout) {
  if (!cq->interrupt.handle) return;
  DPA_DEBUG("Wait for completion queue interrupt\n");
  dpa_error_t error = wait_interrupt(cq->interrupt.handle, timeout);
  if (error != DPA_ERR_OK) return;
  struct fi_cq_err_entry entry = {
    .op_context = NULL,
    .flags = cq->interrupt.event_flags,
    .len = 0,
    .buf = NULL,
    .data = cq->interrupt.id
  };
  cq_add(cq, &entry);
}

static inline void make_cq_progress(dpa_fid_cq* cq, int timeout) {
  timeout = make_queue_progress(&cq->progress, timeout);
  wait_cq_interrupt(cq, timeout);
}

static int dpa_cq_ops_open(struct fid *fid, const char *name,
                           uint64_t flags, void **ops, void *context){
  if (strcmp(name, FI_DPA_CQ_OPS_OPEN)) return -FI_ENODATA;

  *ops = &dpa_ops_cq;
  return 0;
}
