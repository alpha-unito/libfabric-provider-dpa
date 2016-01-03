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
typedef struct dpa_fid_av dpa_fid_av;

#ifndef DPA_AV_H
#define DPA_AV_H

#include "dpa.h"
#include "dpa_domain.h"
#include "fi_ext_dpa.h"

struct dpa_fid_av {
  struct fid_av av;
  dpa_fid_domain* domain;
  int type;
  dpa_addr_t* table;
  size_t count;
  size_t last;
};

int dpa_av_open(struct fid_domain *domain, struct fi_av_attr *attr,
                struct fid_av **av, void *context);

int dpa_av_lookup(struct fid_av *av, fi_addr_t fi_addr, void *addr,
                         size_t *addrlen);

static inline fi_addr_t _get_fi_addr(dpa_fid_av* av, int i) {
  return av->type == FI_AV_TABLE ? (fi_addr_t) i : (fi_addr_t) &av->table[i];
}

static inline fi_addr_t lookup_fi_addr(dpa_fid_av* av, dpa_addr_t* addr) {
  if (!av || !addr) return FI_ADDR_NOTAVAIL;
  for (int i = 0; i < av->last; i++)
    if (av->table[i].nodeId == addr->nodeId &&
        av->table[i].segmentId == addr->segmentId)
      return _get_fi_addr(av, i);
  return FI_ADDR_NOTAVAIL;
}

#endif
