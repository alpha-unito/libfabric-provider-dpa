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
typedef struct dpa_fid_cq dpa_fid_cq;
typedef struct dpa_cq_event dpa_cq_event;

#ifndef DPA_CQ_H
#define DPA_CQ_H

#include "dpa_ep.h"
#include "dpa_domain.h"
#include "dpa.h"

struct _cq_interrupt {
  dpa_local_interrupt_t handle;
  uint64_t event_flags;
  dpa_intid_t id;
  dpa_desc_t sd;
};

struct _cq_progress {
  progress_queue_t func;
  void* arg;
};
  

struct dpa_fid_cq {
  struct fid_cq cq;
  dpa_fid_domain* domain;
  fastlock_cond_t cond;
  size_t entry_size;
  struct slist event_queue;
  struct slist error_queue;
  struct slist free_list;
  struct _cq_interrupt interrupt;
  struct _cq_progress progress;
};

struct dpa_cq_event {
  struct fi_cq_err_entry entry;
  fi_addr_t src_addr;
  struct slist_entry list_entry;
};

int dpa_cq_open(struct fid_domain *domain, struct fi_cq_attr *attr, struct fid_cq **cq, void *context);
void cq_add(dpa_fid_cq* cq, struct fi_cq_err_entry* entry, fi_addr_t src_addr);

#endif
