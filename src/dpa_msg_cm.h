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
typedef struct control_data control_data;
typedef struct segment_data segment_data;
typedef struct msg_data msg_data;
typedef struct buffer_status buffer_status;
typedef struct local_buffer_info local_buffer_info;
typedef struct msg_local_segment_info msg_local_segment_info;
typedef struct msg_queue msg_queue;
typedef struct msg_queue_entry msg_queue_entry;
typedef struct ep_recv_info ep_recv_info;
typedef struct ep_send_info ep_send_info;
typedef struct msg_queue_ptr_entry msg_queue_ptr_entry;

#ifndef _DPA_MSG_CM_H
#define _DPA_MSG_CM_H

#include "dpa_segments.h"
#include "fi_ext_dpa.h"
#include "dpa_cm.h"

struct segment_data {
  dpa_segmid_t segmentId;
  size_t offset;
  size_t size;
  dpa_intid_t recvInterruptId;
  dpa_intid_t sendInterruptId;
};

struct control_data {
  dpa_nodeid_t nodeId;
  segment_data local_segment_data;
  segment_data remote_segment_data;
};

struct buffer_status {
  size_t read;
  char data[0];
};

struct msg_data {
  size_t size;
  char data[0];
};

struct local_buffer_info {
  volatile buffer_status* base;
  size_t size;
  msg_local_segment_info* segment;
};

struct msg_local_segment_info {
  local_segment_info segment_info;
  int bufcount;
  local_buffer_info* buffers;
};

struct ep_recv_info {
  dpa_desc_t sd;
  local_buffer_info* buffer;
  dpa_local_interrupt_t interrupt;
  dpa_remote_interrupt_t remote_interrupt;
  volatile buffer_status* remote_status;
  size_t read;
  slist msg_queue;
  slist free_entries;
};


struct ep_send_info {
  dpa_desc_t sd;
  dpa_remote_segment_t remote_segment;
  dpa_map_t remoteMap;
  dpa_sequence_t sequence;
  volatile void* remote_buffer;
  dpa_local_interrupt_t interrupt;
  dpa_remote_interrupt_t remote_interrupt;
  volatile buffer_status* remote_status;
  size_t offset;
  size_t size;
  size_t write;
  slist msg_queue;
  slist free_entries;
};

#include "dpa_ep.h"
struct msg_queue_entry {
  dpa_fid_ep* ep;
  const void* buf;
  size_t len;
  uint64_t flags;
  void* context;
  slist_entry list_entry;
};

struct msg_queue_ptr_entry {
  slist_entry list_entry;
  msg_queue_entry entries[0];
};

dpa_error_t ctrl_connect_msg(dpa_fid_ep* ep, volatile segment_data* remote_segment_data);
dpa_error_t connect_msg(dpa_fid_ep* ep, segment_data remote_segment_data);
dpa_error_t disconnect_msg(dpa_fid_ep* ep);
dpa_error_t accept_msg(dpa_fid_ep* ep);

#endif
