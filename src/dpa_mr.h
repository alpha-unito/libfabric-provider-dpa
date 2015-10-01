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
typedef struct dpa_fid_mr dpa_fid_mr;

#ifndef _DPA_MR_H
#define _DPA_MR_H

#include "dpa_domain.h"
#include "dpa_segments.h"

struct dpa_fid_mr {
  struct fid_mr mr;
  local_segment_info segment_info;
  dpa_fid_domain* domain;
  const void* buf;
  size_t len;
  uint64_t access;
  uint64_t flags;
};

void dpa_mr_init();
void dpa_mr_fini();

int dpa_mr_reg(struct fid *fid, const void *buf, size_t len,
               uint64_t access, uint64_t offset, uint64_t requested_key,
               uint64_t flags, struct fid_mr **mr, void *context);

#endif
