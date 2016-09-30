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
typedef struct dpa_fid_ep dpa_fid_ep;
typedef struct dpa_fid_pep dpa_fid_pep;
#ifndef _DPA_EP_H
#define _DPA_EP_H

#include "dpa_cm.h"
#include "dpa_msg_cm.h"
#include "dpa_segments.h"
#include "dpa_eq.h"
#include "dpa_cq.h"
#include "dpa.h"
#include "dpa_av.h"
#include "dpa_mr.h"
#include "dpa_cntr.h"

struct dpa_fid_pep {
  struct fid_pep pep;
  struct fid_fabric* fabric;
  struct fi_info* info;
  dpa_fid_eq* eq;
  dpa_fid_mr* mr;
  dpa_desc_t sd;
  dpa_intid_t interruptId;
  dpa_local_data_interrupt_t interrupt;
  fastlock_t lock;
};

typedef struct remote_mr_cache {
  dpa_addr_t target;
  dpa_desc_t sd;
  dpa_remote_segment_t segment;
  dpa_map_t map;
  dpa_intid_t interruptId;
  dpa_remote_interrupt_t interrupt;
  dpa_sequence_t sequence;
  volatile void* base;
  size_t len;
} remote_mr_cache;

struct dpa_fid_ep {
  struct fid_ep ep;
  dpa_fid_pep* pep;
  dpa_fid_domain* domain;
  uint64_t caps;
  dpa_fid_cq* send_cq;
  dpa_fid_cq* recv_cq;
  dpa_fid_cq* read_cq;
  dpa_fid_cq* write_cq;
  dpa_fid_cntr* send_cntr;
  dpa_fid_cntr* recv_cntr;
  dpa_fid_cntr* read_cntr;
  dpa_fid_cntr* write_cntr;
  dpa_fid_av* av;
  dpa_fid_mr* mr;
  dpa_fid_eq* eq;
  ep_recv_info msg_recv_info;
  ep_send_info msg_send_info;
  slist free_entries_ptrs;
  remote_mr_cache last_remote_mr;
  dpa_addr_t peer_addr;
  segment_data connect_data;
  dpa_desc_t connect_sd;
  dpa_local_data_interrupt_t connect_interrupt;
  uint8_t connected;
  uint8_t lock_needed;
};

int dpa_rdm_verify_attr(struct fi_ep_attr *ep_attr, struct fi_tx_attr *tx_attr, struct fi_rx_attr *rx_attr);

int dpa_ep_open(struct fid_domain *domain, struct fi_info *info,
                 struct fid_ep **ep, void *context);
int dpa_passive_ep_open(struct fid_fabric *fabric, struct fi_info *info,
                        struct fid_pep **pep, void *context);

static inline void lock_if_needed(dpa_fid_ep* ep, slist* list) {
  if (ep->lock_needed) slist_lock(list);
}

static inline void unlock_if_needed(dpa_fid_ep* ep, slist* list) {
  if (ep->lock_needed) slist_unlock(list);
}

#endif
