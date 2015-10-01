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

typedef unsigned int dpa_nodeid_t;
typedef unsigned int dpa_segmid_t;
typedef unsigned int dpa_intid_t;
typedef unsigned int dpa_adapterno_t;


#ifndef _DPA_EXT_DPA_H
#define _DPA_EXT_DPA_H

#include <stddef.h>
#include <stdint.h>
#include <rdma/fi_eq.h>

struct dpa_addr_t {
  dpa_nodeid_t nodeId;
  dpa_segmid_t segmentId;
};

#define FI_DPA_CQ_OPS_OPEN "FI_DPA_CQ_OPS_OPEN"

struct fi_dpa_ops_cq {
  size_t size;
  int (*wait_data)(struct fid_cq* cq, uint64_t* data, uint64_t flags);
};

#endif
