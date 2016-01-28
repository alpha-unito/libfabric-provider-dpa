/* dpa_cntr.h --- 
 * Copyright (c) 2015 University of Torino, Italy */

/* This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 * 
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 * 
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 * 
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *  *
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
 *     Marco Aldinucci (UniTO-A3Cube CSO): code design supervision
 */
typedef struct dpa_fid_cntr dpa_fid_cntr;

#ifndef _DPA_CNTR_H
#define _DPA_CNTR_H

#include "dpa.h"
#include "locks.h"
#include "dpa_eq.h"
#include "dpa_domain.h"

struct dpa_fid_cntr {
  struct fid_cntr cntr;
  dpa_fid_domain* domain;
  void (*inc)(dpa_fid_cntr *cntr);
  void (*err_inc)(dpa_fid_cntr *cntr);
  uint64_t counter;
  uint64_t err;
  atomic_t counter_atomic;
  atomic_t err_atomic;
  queue_progress progress;
  enum fi_wait_obj wait_obj;
};

int dpa_cntr_open(struct fid_domain *domain, struct fi_cntr_attr *attr,
                 struct fid_cntr **cntr, void *context);

static inline void dpa_cntr_inc(dpa_fid_cntr* cntr) {
  cntr->inc(cntr);
}

static inline void dpa_cntr_err_inc(dpa_fid_cntr* cntr) {
  cntr->err_inc(cntr);
}
#endif

