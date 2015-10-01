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
#include "dpa_cm.h"
#include "dpa_msg_cm.h"
#include "dpa_env.h"
#include "dpa_msg.h"

#ifndef BUFFER_SIZE_DEFAULT
#define BUFFER_SIZE_DEFAULT (64 * (1<<10)) //64kB
#endif
#ifndef BUFFERS_PER_SEGMENT_DEFAULT
#define BUFFERS_PER_SEGMENT_DEFAULT 16 //16 * 64kB = 1MB per segment
#endif
DEFINE_ENV_CONST(size_t, BUFFER_SIZE, BUFFER_SIZE_DEFAULT);
DEFINE_ENV_CONST(size_t, BUFFERS_PER_SEGMENT, BUFFERS_PER_SEGMENT_DEFAULT);

#ifndef MIN_MSG_SEGMID_DEFAULT
#define MIN_MSG_SEGMID_DEFAULT (~((~(dpa_segmid_t)0) >> 1))
#endif
#ifndef MAX_MSG_SEGMID_DEFAULT
#define MAX_MSG_SEGMID_DEFAULT (MIN_MSG_SEGMID_DEFAULT + 1024)
#endif
DEFINE_ENV_CONST(dpa_segmid_t, MIN_MSG_SEGMID, MIN_MSG_SEGMID_DEFAULT);
DEFINE_ENV_CONST(dpa_segmid_t, MAX_MSG_SEGMID, MAX_MSG_SEGMID_DEFAULT);

#define NUM_SEGMENTS (MAX_MSG_SEGMID - MIN_MSG_SEGMID)
#define MAX_PEERS (NUM_SEGMENTS * BUFFERS_PER_SEGMENT)
#define CONTROL_SEGMENT_SIZE (MAX_PEERS*sizeof(struct control_data))
#define DATA_SEGMENT_SIZE (BUFFER_SIZE * BUFFERS_PER_SEGMENT)

struct assign_data assign_data;

msg_local_segment_info* local_segments_info;
slist free_msg_queue_entries;

void print_control_data(volatile control_data* data) {
  DPA_DEBUG("nodeId: %u\n", data->nodeId);
  DPA_DEBUG("local-> segmentId: %u, size: %u, offset: %u, recvint: %u, sendint: %u\n", 
            data->local_segment_data.segmentId, data->local_segment_data.size, 
            data->local_segment_data.offset, data->local_segment_data.recvInterruptId,
            data->local_segment_data.sendInterruptId);
  DPA_DEBUG("remote-> segmentId: %u, size: %u, offset: %u, recvint: %u, sendint: %u\n", 
            data->remote_segment_data.segmentId, data->remote_segment_data.size, 
            data->remote_segment_data.offset, data->remote_segment_data.recvInterruptId,
            data->remote_segment_data.sendInterruptId);
}

static dpa_callback_action_t process_send_queue_interrupt_callback(void *, dpa_local_interrupt_t,
                                                                   dpa_error_t);
static dpa_callback_action_t process_recv_queue_interrupt_callback(void *, dpa_local_interrupt_t,
                                                                   dpa_error_t);

static inline void create_data_segment(msg_local_segment_info *info) {
  dpa_error_t error = dpa_alloc_segment(&info->segment_info, assign_data.currentSegmentId,
                                        DATA_SEGMENT_SIZE, zero_segment_initializer, NULL, NULL);
  if (error != DPA_ERR_OK) {
    dpa_destroy_segment(info->segment_info);
    info->segment_info.segmentId = 0;
    return;
  }
  info->bufcount = 0;
  info->buffers = calloc(BUFFERS_PER_SEGMENT, sizeof(local_buffer_info));
  for (int i = 0; i < BUFFERS_PER_SEGMENT; i++) {
    info->buffers[i].base = info->segment_info.base + i * BUFFER_SIZE;
    info->buffers[i].segment = info;
  }
  assign_data.currentSegmentId++;
}

static inline local_buffer_info* get_empty_buffer(){
  //get an empty buffer somewhere
  for (int i = 0; i < NUM_SEGMENTS; i++) {
    msg_local_segment_info* info = &local_segments_info[i];
    while (!info->segment_info.segmentId){
      create_data_segment(info);
    }
    if (info->bufcount < BUFFERS_PER_SEGMENT) {
      for (int j = 0; j < BUFFERS_PER_SEGMENT; j++) {
        if (info->buffers[j].size == 0)
          return &info->buffers[j];
      }
    }
  }
  DPA_WARN("No free buffers available\n");
  return NULL;
}

static inline volatile dpa_error_t alloc_msg_buffer(dpa_fid_ep* ep, volatile segment_data* local_segment_data) {
  dpa_error_t error, nocheck;
  local_buffer_info* empty_buffer = get_empty_buffer();
  if (empty_buffer == NULL) {
    DPA_WARN("No empty buffers available for node %d\n", ep->peer_addr.nodeId);
    error = DPA_ERR_SYSTEM;
    goto alloc_fail;
  }
  DPA_DEBUG("Opening recv virtual device\n");
  DPAOpen(&ep->msg_recv_info.sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAOpen, goto alloc_fail);

  unsigned int interrupt_flags = ep->domain->data_progress == FI_PROGRESS_AUTO
    ? DPA_FLAG_USE_CALLBACK
    : NO_FLAGS;  
  DPA_DEBUG("Creating recv interrupt\n");
  DPACreateInterrupt(ep->msg_recv_info.sd,
                     &ep->msg_recv_info.interrupt, localAdapterNo,
                     (dpa_intid_t*)&(local_segment_data->recvInterruptId),
                     process_recv_queue_interrupt_callback,
                     ep, interrupt_flags, &error);
  DPALIB_CHECK_ERROR(DPACreateInterrupt, goto alloc_recvclose);
  DPA_DEBUG("Opening send virtual device\n");
  DPAOpen(&ep->msg_send_info.sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAOpen, goto alloc_remrecvint);
  DPA_DEBUG("Creating send interrupt\n");
  DPACreateInterrupt(ep->msg_send_info.sd,
                     &ep->msg_send_info.interrupt, localAdapterNo,
                     (dpa_intid_t*)&(local_segment_data->sendInterruptId),
                     process_send_queue_interrupt_callback,
                     ep, interrupt_flags, &error);
  DPALIB_CHECK_ERROR(DPACreateInterrupt, goto alloc_sendclose);
  //reserve buffer
  empty_buffer->size = BUFFER_SIZE;
  //clean buffer before making available
  empty_buffer->base->read = 0;
  ep->msg_recv_info.buffer = empty_buffer;
  ep->msg_recv_info.read = 0;
  recv_read_ptr(&ep->msg_recv_info)->size = 0;
  ep->msg_send_info.remote_status = empty_buffer->base;
  void* segment_base = (void*) empty_buffer->segment->segment_info.base;
  //set metadata
  local_segment_data->segmentId = empty_buffer->segment->segment_info.segmentId;
  local_segment_data->offset = (size_t) (((void*) empty_buffer->base) - segment_base);
  // TODO memory fence here
  local_segment_data->size = empty_buffer->size;
  return DPA_ERR_OK;
        
  //handle dpalib failures
 alloc_sendclose:
  DPAClose(ep->msg_send_info.sd, NO_FLAGS, &nocheck);
 alloc_remrecvint:
  DPARemoveInterrupt(ep->msg_recv_info.interrupt, NO_FLAGS, &nocheck);
 alloc_recvclose:
  DPAClose(ep->msg_send_info.sd, NO_FLAGS, &nocheck);
 alloc_fail:
  DPA_DEBUG("Node %d failed to connect\n",  ep->peer_addr.nodeId);
  return error;
}
volatile control_data* write_msg_accept_data(dpa_fid_ep* ep) {
  volatile control_data* result;
  THREADSAFE(&ep->pep->lock, ({
        volatile control_data* ctrlseg = (volatile control_data*) ep->pep->control_info.base;
        result = &ctrlseg[ep->pep->control_index];
        volatile segment_data* local_segment_data = &result->local_segment_data;
        dpa_error_t error = alloc_msg_buffer(ep, local_segment_data);
        if (error == DPA_ERR_OK) {
          /* node MUST be set AFTER all other metadata, 
           * so that when remote node reads this it can be
           * sure everything else is already there.
           * volatile ensures ordering is honored by the compiler;
           * might need a memory fence to ensure also memory ordering */
          result->nodeId = ep->peer_addr.nodeId;
          print_control_data(&ctrlseg[ep->pep->control_index]);
          ep->pep->control_index++;
        }
        else result = NULL;
      }));
  return result;
}
  

void remove_msg_accept_data(dpa_fid_ep* ep, volatile control_data* data){
  THREADSAFE(&ep->pep->lock, ({
        volatile control_data* ctrlseg = (volatile control_data*) ep->pep->control_info.base;
        *data = ctrlseg[ep->pep->control_index];
        ep->pep->control_index--;
      }));
}

dpa_error_t accept_msg(dpa_fid_ep* ep) { 
  DPA_DEBUG("Accepting connection from endpoint %d:%d\n", ep->peer_addr.nodeId, ep->peer_addr.segmentId);
  volatile control_data* control_data = write_msg_accept_data(ep);
  if (!control_data)
    return -FI_ENOSPC;
  print_control_data(control_data);
  // wait until remote writes its data
  while(!control_data->remote_segment_data.size);
  print_control_data(control_data);
  dpa_error_t error = connect_msg(ep, control_data->remote_segment_data);
  remove_msg_accept_data(ep, control_data);
  return error == DPA_ERR_OK ? FI_SUCCESS : -FI_ECONNABORTED;
}

dpa_error_t ctrl_connect_msg(dpa_fid_ep* ep, volatile segment_data* remote_segment_data) {
  dpa_error_t error, nocheck;
  dpa_remote_segment_t remoteControlSegment;
  dpa_desc_t sd;
  DPA_DEBUG("Opening new virtual device\n");
  DPAOpen(&sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAOpen, goto ctrl_end);
  
  DPA_DEBUG("Connecting to remote control segment %d on node %d\n",
            ep->peer_addr.segmentId, ep->peer_addr.nodeId);
  do {
    DPAConnectSegment(sd, &remoteControlSegment, ep->peer_addr.nodeId,
                      ep->peer_addr.segmentId, localAdapterNo, NO_CALLBACK,
                      NULL, DPA_INFINITE_TIMEOUT, NO_FLAGS, &error);
  } while (error != DPA_ERR_OK);
  
  DPA_DEBUG("Mapping remote control segment\n");
  dpa_map_t remoteControlMap;
  volatile control_data* remote_ctrl_segment = DPAMapRemoteSegment(remoteControlSegment,
                                                                   &remoteControlMap,
                                                                   0, CONTROL_SEGMENT_SIZE,
                                                                   NULL, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAMapRemoteSegment, goto ctrl_disconnect);
  DPA_DEBUG("Looking for current node segment data on remote\n");
  int i = 0;
  while (remote_ctrl_segment[i].nodeId != localNodeId) {
    print_control_data(&remote_ctrl_segment[i]);
    if (remote_ctrl_segment[i].local_segment_data.size > 0) i++;
    else i = 0;
  }
  print_control_data(&remote_ctrl_segment[i]);
  DPA_DEBUG("Fetching remote segment data for current node\n");
  *remote_segment_data = remote_ctrl_segment[i].local_segment_data;
  error = alloc_msg_buffer(ep, &remote_ctrl_segment[i].remote_segment_data);
  print_control_data(&remote_ctrl_segment[i]);
 ctrl_unmap:
  DPAUnmapSegment(remoteControlMap, NO_FLAGS, &nocheck);
 ctrl_disconnect:
  DPADisconnectSegment(remoteControlSegment, NO_FLAGS, &nocheck);
 ctrl_close:
  DPAClose(sd, NO_FLAGS, &nocheck);
 ctrl_end:
  return error;
}

dpa_error_t connect_msg(dpa_fid_ep* ep, segment_data remote_segment_data) {
  dpa_error_t error;
  DPA_DEBUG("Connecting to remote recv segment %u on node %u\n", 
            remote_segment_data.segmentId, ep->peer_addr.nodeId);
  DPAConnectSegment(ep->msg_send_info.sd, &ep->msg_send_info.remote_segment, ep->peer_addr.nodeId,
                    remote_segment_data.segmentId, localAdapterNo, NO_CALLBACK,
                    NULL, DPA_INFINITE_TIMEOUT, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAConnectSegment, goto conn_end);

  DPA_DEBUG("Connecting to remote recv interrupt %u on node %u\n", 
            remote_segment_data.recvInterruptId, ep->peer_addr.nodeId);
  DPAConnectInterrupt(ep->msg_send_info.sd, &ep->msg_send_info.remote_interrupt,
                      ep->peer_addr.nodeId, localAdapterNo, remote_segment_data.recvInterruptId,
                      DPA_INFINITE_TIMEOUT, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAConnectInterrupt, goto conn_end);

  DPA_DEBUG("Connecting to remote send interrupt %u on node %u\n", 
            remote_segment_data.sendInterruptId, ep->peer_addr.nodeId);
  DPAConnectInterrupt(ep->msg_recv_info.sd, &ep->msg_recv_info.remote_interrupt,
                      ep->peer_addr.nodeId, localAdapterNo, remote_segment_data.sendInterruptId,
                      DPA_INFINITE_TIMEOUT, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAConnectInterrupt, goto conn_end);

  DPA_DEBUG("Mapping remote segment %u on node %u\n", 
            remote_segment_data.segmentId, ep->peer_addr.nodeId);
  volatile buffer_status* remote_status = DPAMapRemoteSegment(ep->msg_send_info.remote_segment,
                                                        &ep->msg_send_info.remoteMap,
                                                        remote_segment_data.offset,
                                                        remote_segment_data.size,
                                                        NULL, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAMapRemoteSegment, goto conn_end);
  
  DPA_DEBUG("Saving remote segment data\n");
  ep->msg_send_info.write = 0;
  ep->msg_send_info.size = remote_segment_data.size - offsetof(buffer_status, data);
  ep->msg_send_info.remote_buffer = remote_status->data;
  ep->msg_recv_info.remote_status = remote_status;

  DPA_DEBUG("Adding CONNECTED event to event queue\n");
  struct fi_eq_cm_entry event = {
    .fid = &ep->ep.fid,
    .info = NULL
  };
  eq_add(ep->eq, FI_CONNECTED, &event, sizeof(event), NO_FLAGS, 0);
  ep->connected = 1;
 conn_end:
  return error;  
}

dpa_error_t disconnect_msg(dpa_fid_ep* ep) {
  dpa_error_t error, result = DPA_ERR_OK;
  if (ep->msg_send_info.remote_segment) {
    DPA_DEBUG("Disconnecting from remote recv segment\n");
    DPADisconnectSegment(ep->msg_send_info.remote_segment, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPADisconnectSegment, result = error);
  }
  if (ep->msg_send_info.remoteMap) {
    DPA_DEBUG("Unmapping remote recv segment\n");
    DPAUnmapSegment(ep->msg_send_info.remoteMap, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPAUnmapSegment, result = error);
  }    
  if (ep->msg_send_info.remote_interrupt) {
    DPA_DEBUG("Disconnecting from remote recv interrupt\n");
    DPADisconnectInterrupt(ep->msg_send_info.remote_interrupt, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPADisconnectInterrupt, result = error);
  }
  if (ep->msg_recv_info.remote_interrupt) {
    DPA_DEBUG("Disconnecting from remote send interrupt\n");
    DPADisconnectInterrupt(ep->msg_recv_info.remote_interrupt, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPADisconnectInterrupt, result = error);
  }
  ep->connected = 0;
  return result;
}

static dpa_callback_action_t process_send_queue_interrupt_callback(void *arg,
                                                                   dpa_local_interrupt_t interrupt,
                                                                   dpa_error_t status){
  dpa_fid_ep* ep = *((dpa_fid_ep **)arg);
  process_send_queue(ep, 0);
  return DPA_CALLBACK_CONTINUE;
}

static dpa_callback_action_t process_recv_queue_interrupt_callback(void *arg,
                                                                   dpa_local_interrupt_t interrupt,
                                                                   dpa_error_t status){
  dpa_fid_ep* ep = *((dpa_fid_ep **)arg);
  process_recv_queue(ep, 0);
  return DPA_CALLBACK_CONTINUE;
}
 
