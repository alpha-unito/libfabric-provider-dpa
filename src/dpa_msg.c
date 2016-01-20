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
#define LOG_SUBSYS FI_LOG_EP_DATA
#include "dpa.h"
#include "dpa_segments.h"
#include "dpa_cm.h"
#include "dpa_msg.h"


msg_queue_entry* get_free_entry(dpa_fid_ep* ep, slist* free_entries) {
  msg_queue_entry* result;
  if (slist_empty(free_entries))
    create_msg_queue_entries(ep, free_entries);
  slist_entry* head = slist_remove_head_unsafe(free_entries);
  return container_of(head, msg_queue_entry, list_entry);
}

int dpa_msg_init() {
}

int dpa_msg_fini() {
}

typedef void (*process_queue_t)(dpa_fid_ep* ep, uint8_t locked);

static inline ssize_t _dpa_msg_enqueue(msg_queue_entry* msg, dpa_fid_ep* ep, slist* msg_queue, slist* free_entries) {
  msg_queue_entry* entry = get_free_entry(ep, free_entries);
  memcpy(entry, msg, sizeof(msg_queue_entry));
  slist_insert_tail_unsafe(&entry->list_entry, msg_queue);
  unlock_if_needed(ep, msg_queue);
  return FI_SUCCESS;
}
static inline int try_recv(msg_queue_entry* entry);
static inline int try_send(msg_queue_entry* entry);

static inline ssize_t _dpa_recv(dpa_fid_ep* ep, const void *buf, size_t len, 
                                uint64_t flags, void* context) {
                                  
  slist* msg_queue = &ep->msg_recv_info.msg_queue;
  slist* free_entries = &ep->msg_recv_info.free_entries;
  msg_queue_entry entry = {
    .ep = ep,
    .buf = buf,
    .len = len,
    .flags = flags,
    .context = context
  };
  lock_if_needed(ep, msg_queue);
  int err = -FI_EAGAIN;
  // try receive if noone is trying and endpoint is connected
  if (slist_empty(msg_queue) && ep->connected)
    err = try_recv(&entry);
    

  if(err == -FI_EAGAIN) {
    DPA_DEBUG("Enqueuing receive\n");
    return _dpa_msg_enqueue(&entry, ep, msg_queue, free_entries);
  } else {
    return FI_SUCCESS;
  }
}

ssize_t dpa_recv(struct fid_ep *ep, void *buf, size_t len, void *desc,
				 fi_addr_t src_addr, void *context){
  return _dpa_recv(container_of(ep, dpa_fid_ep, ep), buf, len, NO_FLAGS, context);
}

ssize_t dpa_recvv(struct fid_ep *ep, const struct iovec *iov, void **desc,
                   size_t count, fi_addr_t src_addr, void *context){
  const struct fi_msg msg = {
    .msg_iov = iov,
    .desc = desc,
    .iov_count = count,
    .addr = src_addr,
    .context = context,
    .data = 0
  };
  return dpa_recvmsg(ep, &msg, NO_FLAGS);
}

inline ssize_t dpa_recvmsg(struct fid_ep *ep, const struct fi_msg *msg, uint64_t flags) {
  if (!msg || (msg->iov_count && !msg->msg_iov) || msg->iov_count > 1)
    return -FI_EINVAL;

  void* buf = NULL;
  size_t len = 0;
  if (msg->iov_count) {
    buf = msg->msg_iov[0].iov_base;
    len = msg->msg_iov[0].iov_len;
  }
  dpa_fid_ep* ep_priv = container_of(ep, dpa_fid_ep, ep);
  return _dpa_recv(ep_priv, buf, len, flags, msg->context);
}

ssize_t _dpa_send(dpa_fid_ep* ep, const void *buf, size_t len, 
                  uint64_t flags, void* context) {
  slist* msg_queue = &ep->msg_send_info.msg_queue;
  slist* free_entries = &ep->msg_send_info.free_entries;

  msg_queue_entry entry = {
    .ep = ep,
    .buf = buf,
    .len = len,
    .flags = NO_FLAGS,
    .context = context
  };
  lock_if_needed(ep, msg_queue);
  int err = -FI_EAGAIN;
  if (slist_empty(msg_queue))
    err = try_send(&entry);

  if (err == -FI_EAGAIN) {
    DPA_DEBUG("Enqueuing send\n");
    return _dpa_msg_enqueue(&entry, ep, msg_queue, free_entries);
  } else {
    return FI_SUCCESS;
  }
}

ssize_t dpa_send(struct fid_ep *ep, const void *buf, size_t len, void *desc,
				 fi_addr_t dest_addr, void *context) {
  return _dpa_send(container_of(ep, dpa_fid_ep, ep), buf, len, NO_FLAGS, context);
}

ssize_t dpa_sendv(struct fid_ep *ep, const struct iovec *iov, void **desc,
                  size_t count, fi_addr_t dest_addr, void *context){
  const struct fi_msg msg = {
    .msg_iov = iov,
    .desc = desc,
    .iov_count = count,
    .addr = dest_addr,
    .context = context,
    .data = 0
  };
  return dpa_sendmsg(ep, &msg, NO_FLAGS);
}

inline ssize_t dpa_sendmsg(struct fid_ep *ep, const struct fi_msg *msg, uint64_t flags){
  if (!msg || (msg->iov_count && !msg->msg_iov) || msg->iov_count > 1)
    return -FI_EINVAL;

  void* buf = NULL;
  size_t len = 0;
  if (msg->iov_count) {
    buf = msg->msg_iov[0].iov_base;
    len = msg->msg_iov[0].iov_len;
  }
  dpa_fid_ep* ep_priv = container_of(ep, dpa_fid_ep, ep);
  return _dpa_send(ep_priv, buf, len, flags, msg->context);
}

/* The following two functions are useful to handle recvv and sendv. 
However, they have been removed for performance reasons 
(to handle iovecs in msg_queue, you must allocate memory)
* TODO allocate memory only if iov_count > 1 and re-enable this

static inline size_t scatter_gather(const struct iovec* dest, size_t dest_count, const struct iovec* src, size_t src_count) {
  if(!dest_count || !src_count) return 0;
  
  void *dest_base = dest[0].iov_base, *src_base = src[0].iov_base;
  size_t dest_size = dest[0].iov_len, src_size = src[0].iov_len;
  size_t dest_i = 1, src_i = 1;
  size_t copied = 0;
  
#define IOV_UPD(iovname)                                     \
  iovname##_base += copy_len;                                \
  iovname##_size -= copy_len;                                \
  if (iovname##_size == 0) {                                 \
    iovname##_base = iovname[iovname##_i].iov_base;        \
    iovname##_size = iovname[iovname##_i].iov_len;         \
    iovname##_i++;                                           \
  }
  
  while (dest_i <= dest_count && src_i <= src_count) {
    size_t copy_len =  MIN(dest_size, src_size);
    memcpy(dest_base, src_base, copy_len);

    copied += copy_len;
    IOV_UPD(dest);
    IOV_UPD(src);
  }
  return copied;
}

static inline size_t split_buffer(volatile buffer_status* base, size_t buffer_size, volatile void* data, size_t msg_size, struct iovec* buf) {
  void* buffer_top = ((void*)base) + buffer_size;
  size_t avail_size = MIN(buffer_top - data, msg_size);
  buf[0].iov_base = (void*) data;
  buf[0].iov_len = avail_size;
  if (avail_size < msg_size) {
    buf[1].iov_base = ((void*) base) + sizeof(buffer_status);
    buf[1].iov_len = msg_size - avail_size;
    return 2;
  }
  else
    return 1;
}
*/

static inline size_t new_offset(size_t prev_offset, size_t msg_size, size_t buf_size) {
  /* 2*buf_size accomodates for page info, so when writer wraps buffer 
   * knows if receiver has done so as well */
  return (prev_offset + msg_size + offsetof(msg_data, data)) % (2*buf_size);
}

static inline size_t read_msg(msg_queue_entry* msg, ep_recv_info* recv_info) {
  local_buffer_info* buf_info = recv_info->buffer;
  volatile msg_data* read_ptr = recv_read_ptr(recv_info);
  size_t buftop_size = ((void*)buf_info->base->data) + recv_buffer_size(recv_info) - (void*)read_ptr->data;
  size_t msg_size = read_ptr->size;
  size_t recv_size = msg->len;
  size_t read_size = MIN(recv_size, msg_size);
  size_t copy_size = MIN(read_size, buftop_size);
  /*
  printf("local memory dump\n");
  for (int i = 0; i < recv_buffer_size(recv_info); i++){
    uint8_t* ptr = (((uint8_t*)buf_info->base->data)+i);
    if (ptr == (uint8_t*)read_ptr) printf("r");
    printf("%02x ", *ptr);
  }
  printf("\n");
  printf("read = %u\n\n", recv_info->read);
  */
  DPA_DEBUG("Reading %u bytes, message is %u bytes\n", read_size, msg_size);
  memcpy((void*)msg->buf, (void*)read_ptr->data, copy_size);
  //clean up read buffer
  memset((void*)read_ptr, 0, offsetof(msg_data, data) + copy_size);
  if (copy_size < read_size) {
  // copy second part if we need to wrap around buffer
    memcpy((void*)msg->buf + copy_size, (void*)buf_info->base->data, read_size - copy_size);
    //clean up buffer
    memset((void*)buf_info->base->data, 0, read_size - copy_size);
  }
  
  recv_info->read = new_offset(recv_info->read, msg_size, recv_buffer_size(recv_info));
  // write remote status
  recv_info->remote_status->read = recv_info->read;

  DPA_DEBUG("Triggering remote interrupt\n");
  dpa_error_t nocheck;
  DPATriggerInterrupt(recv_info->remote_interrupt, NO_FLAGS, &nocheck);

  return read_size;
}

static inline int try_recv(msg_queue_entry* entry) {
  dpa_fid_ep* ep = entry->ep;
  size_t msg_size = recv_read_ptr(&ep->msg_recv_info)->size;
  if (msg_size<=0) return -FI_EAGAIN;
  size_t copied = read_msg(entry, &ep->msg_recv_info);
  DPA_DEBUG("received msg size: %u, buffer size: %u, copied: %u\n",
            msg_size, entry->len, copied);

  int err = copied < msg_size ? FI_ETOOSMALL : FI_SUCCESS;
  if (ep->recv_cq) {
    // generate completion
    struct fi_cq_err_entry completion = {
      .op_context = entry->context,
      .flags = FI_MSG | FI_RECV,
      .len = copied,
      .buf = (void*)entry->buf,
      .data = 0,
      .err = err,
      .olen = msg_size - copied,
      .prov_errno = DPA_ERR_OK,
      .err_data = NULL
    };
    cq_add(ep->recv_cq, &completion);
  }
  if (ep->recv_cntr) {
    if (err == FI_SUCCESS)
      dpa_cntr_inc(ep->recv_cntr);
    else
      dpa_cntr_err_inc(ep->recv_cntr);
  }
  return FI_SUCCESS;
}

inline void process_recv_queue(dpa_fid_ep* ep, uint8_t locked) {
  slist* queue = &ep->msg_recv_info.msg_queue;
  if (!ep->connected) {
    if (locked) unlock_if_needed(ep, queue);
    return;
  }
  if (!locked) {
    if (slist_empty(queue)) return;
    lock_if_needed(ep, queue);
  }
  local_buffer_info* buffer_info = ep->msg_recv_info.buffer;
  int err = FI_SUCCESS;
  // while there is a posted buffer AND we received a message
  while (!slist_empty(queue) && err == FI_SUCCESS) {
    slist_entry* entry = queue->head;
    msg_queue_entry* head = container_of(entry, msg_queue_entry, list_entry);
    err = try_recv(head);
    if (err == FI_SUCCESS) {
      // put in free list
      slist_remove_head_unsafe(queue);
      slist_insert_head_unsafe(entry, &ep->msg_recv_info.free_entries);
    }
  }
  //remove locks
  unlock_if_needed(ep, queue);
}

static inline void write_msg(ep_send_info* send_info,
                             msg_queue_entry* msg, size_t space_after) {
  volatile void* remote_buffer = send_info->remote_buffer;
  volatile msg_data* data = send_write_ptr(send_info);
  volatile void* recvbuf = data->data;
  size_t buftop_size = remote_buffer + send_info->size - recvbuf;
  size_t copy_size = MIN(msg->len, buftop_size);
  
  DPA_DEBUG("Writing %u bytes\n", msg->len);
  memcpy((void*)recvbuf, (void*)msg->buf, copy_size);

  //check if we need to wrap around buffer
  if (msg->len > buftop_size) {
    recvbuf = remote_buffer;
    copy_size = msg->len - buftop_size;
    memcpy((void*)recvbuf, (void*)msg->buf + buftop_size, copy_size);
  }
  /*
  printf("remote memory dump\n");
  for (int i = 0; i < send_info->size; i++)
    printf("%02x ", *(((uint8_t*)remote_buffer)+i));
  printf("\n");
  printf("write = %u\n\n", send_info->write);
  */
  send_info->write = new_offset(send_info->write, msg->len, send_info->size);

  //TODO remote memory fence here
  data->size = msg->len;

  DPA_DEBUG("Triggering remote interrupt\n");
  dpa_error_t nocheck;
  DPATriggerInterrupt(send_info->remote_interrupt, NO_FLAGS, &nocheck);
}

static inline size_t remote_space(ep_send_info* send_info) {
  size_t read = send_info->remote_status->read;
  /* if read is larger it means it is on the previous page (you can't read what has not been written), 
   * so we remove size from it. Else we are on the same page,
   * but this means read < write even if we can write until
   * top of buffer and wrap around. So read + size */
  if (read > send_info->write)
    read -= send_info->size;
  else
    read += send_info->size;
  return read - send_info->write;
}

int try_send(msg_queue_entry* entry) {
  // check if there is space to write data (according to remote info cache)
  size_t needed_space = entry->len + offsetof(msg_data, data);
  ep_send_info* send_info = &entry->ep->msg_send_info;
  size_t avail_space = remote_space(send_info);
  if (needed_space > avail_space) {
    DPA_DEBUG("Unable to send, need %u bytes, got %u free bytes instead\n", needed_space, avail_space);
    return -FI_EAGAIN;
  }
  //actually write the message on remote buffer.
  write_msg(send_info, entry, avail_space-needed_space);
  if (entry->ep->send_cq) {
    // generate completion
    struct fi_cq_err_entry completion = {
      .op_context = entry->context,
      .flags = FI_MSG | FI_SEND,
      .data = 0,
      .err = FI_SUCCESS
    };
    cq_add(entry->ep->send_cq, &completion);
  }
  if (entry->ep->send_cntr)
    dpa_cntr_inc(entry->ep->send_cntr);
  return FI_SUCCESS;
}
	

void process_send_queue(dpa_fid_ep* ep, uint8_t locked) {
  ep_send_info* send_info = &ep->msg_send_info;
  slist* queue = &(send_info->msg_queue);
  if (!ep->connected) {
    if (locked) unlock_if_needed(ep, queue);
    return;
  }
  if (!locked) {
    if (slist_empty(queue)) return;
    lock_if_needed(ep, queue);
  }
  int err = FI_SUCCESS;
  while (!slist_empty(queue) && err != -FI_EAGAIN) {
    msg_queue_entry* head = container_of(queue->head, msg_queue_entry, list_entry);
    err = try_send(head);
    if (err != -FI_EAGAIN) {
      // move to free queue
      slist_remove_head_unsafe(queue);
      slist_insert_head_unsafe(&head->list_entry, &(send_info->free_entries));
    }
  }
  unlock_if_needed(ep, queue);
}

static inline int progress_queue(dpa_fid_ep* ep, dpa_local_interrupt_t interrupt,
                          int timeout_millis, process_queue_t process_queue) {
  timeout_millis = timeout_millis < 0 ? DPA_INFINITE_TIMEOUT : timeout_millis;
  if (timeout_millis) {
    dpa_error_t error;
    DPAWaitForInterrupt(interrupt, timeout_millis, NO_FLAGS, &error);
    // avoid logging errors for timeout
    if (error == DPA_ERR_TIMEOUT)
      timeout_millis = 0;
    else
      DPALIB_CHECK_ERROR(DPAWaitForLocalSegmentEvent, return 0);
  }
  process_queue(ep, 0);
  return timeout_millis;
}

int progress_send_queue(dpa_fid_ep* ep, int timeout_millis) {
  return progress_queue(ep, ep->msg_send_info.interrupt, timeout_millis, process_send_queue);
}

int progress_recv_queue(dpa_fid_ep* ep, int timeout_millis) {
  return progress_queue(ep, ep->msg_recv_info.interrupt, timeout_millis, process_recv_queue);
}

int progress_sendrecv_queues(dpa_fid_ep* ep, int timeout_millis) {
  int remaining = progress_send_queue(ep, timeout_millis);
  return progress_recv_queue(ep, remaining);
}
