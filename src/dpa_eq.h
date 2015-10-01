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
typedef struct dpa_fid_eq dpa_fid_eq;

#ifndef DPA_EQ_H
#define DPA_EQ_H
#include "dpa.h"
#include "dpa_ep.h"

typedef struct dpa_fid_eq {
  struct fid_eq eq;
  struct fid_fabric* fabric;
  fastlock_cond_t cond;
  struct slist event_queue;
  struct slist error_queue;
  struct slist free_list;
  progress_queue_t progress;
  void* progress_arg;
} dpa_fid_eq;

typedef struct dpa_eq_event {
  int event;
  union {
    struct fi_eq_entry data;
    struct fi_eq_cm_entry cm;
    struct fi_eq_err_entry err;
  } entry;
  int error;
  size_t entry_size;
  struct slist_entry list_entry;
} dpa_eq_event;

int	dpa_eq_open(struct fid_fabric *fabric, struct fi_eq_attr *attr,
                struct fid_eq **eq, void *context);

ssize_t eq_add(dpa_fid_eq* eq, uint32_t event_num, const void* buf, size_t len,
               uint64_t flags, int error);

#endif

