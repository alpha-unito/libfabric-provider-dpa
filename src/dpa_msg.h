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
#ifndef DPA_MSG_H
#define DPA_MSG_H
#include "dpa_ep.h"
#include "dpa_msg_cm.h"

int dpa_msg_init();
int dpa_msg_fini();

ssize_t dpa_recv(struct fid_ep *ep, void *buf, size_t len, void *desc,
				 fi_addr_t src_addr, void *context);
ssize_t dpa_recvv(struct fid_ep *ep, const struct iovec *iov, void **desc,
                  size_t count, fi_addr_t src_addr, void *context);
ssize_t dpa_recvmsg(struct fid_ep *ep, const struct fi_msg *msg, uint64_t flags);
ssize_t dpa_send(struct fid_ep *ep, const void *buf, size_t len, void *desc,
				 fi_addr_t dest_addr, void *context);
ssize_t dpa_sendv(struct fid_ep *ep, const struct iovec *iov, void **desc,
                  size_t count, fi_addr_t dest_addr, void *context);
ssize_t dpa_sendmsg(struct fid_ep *ep, const struct fi_msg *msg, uint64_t flags);

void process_send_queue(dpa_fid_ep* ep, uint8_t locked);
void process_recv_queue(dpa_fid_ep* ep, uint8_t locked);
int progress_send_queue(dpa_fid_ep* ep, int timeout_millis);
int progress_recv_queue(dpa_fid_ep* ep, int timeout_millis);
int progress_sendrecv_queues(dpa_fid_ep* ep, int timeout_millis);

static inline size_t recv_buffer_size(ep_recv_info* recv_info) {
  return recv_info->buffer->size - offsetof(buffer_status, data);
}

static inline volatile msg_data* data_ptr(void* base, size_t offset, size_t buf_size) {
  return (volatile msg_data*) (base + (offset % buf_size));
}

static inline volatile msg_data* recv_read_ptr(ep_recv_info* recv_info) {
  return data_ptr((void*)recv_info->buffer->base->data, recv_info->read, recv_buffer_size(recv_info));
}

static inline volatile msg_data* send_write_ptr(ep_send_info* send_info) {
  return data_ptr((void*)send_info->remote_buffer, send_info->write, send_info->size);
}

#ifndef MSG_QUEUE_ENTRIES_BATCH_SIZE
#define MSG_QUEUE_ENTRIES_BATCH_SIZE 512
#endif

static inline void create_msg_queue_entries(dpa_fid_ep* ep, slist* free_entries) {
  msg_queue_ptr_entry* newentries = malloc(sizeof(msg_queue_ptr_entry) + MSG_QUEUE_ENTRIES_BATCH_SIZE * sizeof(msg_queue_entry));
  lock_if_needed(ep, &ep->free_entries_ptrs);
  slist_insert_head_unsafe(&newentries->list_entry, &ep->free_entries_ptrs);
  unlock_if_needed(ep, &ep->free_entries_ptrs);
  for (int i = 0; i < MSG_QUEUE_ENTRIES_BATCH_SIZE; i++)
    slist_insert_head_unsafe(&newentries->entries[i].list_entry, free_entries);
}
#endif
