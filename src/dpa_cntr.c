/* dpa_cntr.c --- 
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
#include "dpa_cntr.h"
#include "dpa_domain.h"
#include "dpa.h"

int dpa_cntr_close(struct fid* fid);
static struct fi_ops dpa_cntr_fi_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_cntr_close,
  .bind = fi_no_bind,
  .control = fi_no_control,
  .ops_open = fi_no_ops_open
};

int dpa_cntr_wait(struct fid_cntr *cntr, uint64_t threshold, int timeout);

uint64_t dpa_cntr_read_unsafe(struct fid_cntr *cntr);
uint64_t dpa_cntr_readerr_unsafe(struct fid_cntr *cntr);
int dpa_cntr_add_unsafe(struct fid_cntr *cntr, uint64_t value);
static void dpa_cntr_inc_unsafe(dpa_fid_cntr *cntr);
static void dpa_cntr_err_inc_unsafe(dpa_fid_cntr *cntr);
int dpa_cntr_set_unsafe(struct fid_cntr *cntr, uint64_t value);
static struct fi_ops_cntr dpa_fi_ops_cntr_unsafe = {
  .size = sizeof(struct fi_ops_cntr),
  .read = dpa_cntr_read_unsafe,
  .readerr = dpa_cntr_readerr_unsafe,
  .add = dpa_cntr_add_unsafe,
  .set = dpa_cntr_set_unsafe,
  .wait = dpa_cntr_wait
};
uint64_t dpa_cntr_read_safe(struct fid_cntr *cntr);
uint64_t dpa_cntr_readerr_safe(struct fid_cntr *cntr);
int dpa_cntr_add_safe(struct fid_cntr *cntr, uint64_t value);
static void dpa_cntr_inc_safe(dpa_fid_cntr *cntr);
static void dpa_cntr_err_inc_safe(dpa_fid_cntr *cntr);
int dpa_cntr_set_safe(struct fid_cntr *cntr, uint64_t value);
static struct fi_ops_cntr dpa_fi_ops_cntr_safe = {
  .size = sizeof(struct fi_ops_cntr),
  .read = dpa_cntr_read_safe,
  .readerr = dpa_cntr_readerr_safe,
  .add = dpa_cntr_add_safe,
  .set = dpa_cntr_set_safe,
  .wait = dpa_cntr_wait
};

int dpa_cntr_open(struct fid_domain *domain, struct fi_cntr_attr *attr,
                 struct fid_cntr **cntr, void *context){
  if (attr && attr->wait_obj != FI_WAIT_NONE
      && attr->wait_obj != FI_WAIT_UNSPEC) {
    DPA_WARN("Counter wait object can be only FI_WAIT_NONE"
             "or FI_WAIT_UNSPEC");
    return -FI_EINVAL;
  }
  dpa_fid_domain* domain_priv = container_of(domain, dpa_fid_domain, domain);
  dpa_fid_cntr* cntr_priv = ALLOC_INIT(dpa_fid_cntr, {
      .cntr = {
        .fid = {
          .fclass = FI_CLASS_CNTR,
          .context = context,
          .ops = &dpa_cntr_fi_ops,
        }
      },
      .domain = domain_priv,
  });
        
  cntr_priv->cntr.fid.fclass = FI_CLASS_CNTR;
  cntr_priv->cntr.fid.context = context;
  cntr_priv->cntr.fid.ops = &dpa_cntr_fi_ops;
  queue_progress_init(&cntr_priv->progress);
  if (domain_priv->threading >= FI_THREAD_COMPLETION) {
    cntr_priv->counter = 0;
    cntr_priv->err = 0;
    cntr_priv->cntr.ops = &dpa_fi_ops_cntr_unsafe;
    cntr_priv->inc = dpa_cntr_inc_unsafe;
    cntr_priv->err_inc = dpa_cntr_err_inc_unsafe;
  } else {
    atomic_initialize(&cntr_priv->counter_atomic, 0);
    atomic_initialize(&cntr_priv->err_atomic, 0);
    cntr_priv->cntr.ops = &dpa_fi_ops_cntr_safe;
    cntr_priv->inc = dpa_cntr_inc_safe;
    cntr_priv->err_inc = dpa_cntr_err_inc_safe;
  }
  *cntr = &cntr_priv->cntr;
  return FI_SUCCESS;
}

int dpa_cntr_close(struct fid* fid) {
  free(container_of(fid, dpa_fid_cntr, cntr.fid));
  return FI_SUCCESS;
}

uint64_t dpa_cntr_read_unsafe(struct fid_cntr *fid_cntr){
  dpa_fid_cntr* cntr = container_of(fid_cntr, dpa_fid_cntr, cntr);
  make_queue_progress(&cntr->progress, 0);
  return cntr->counter;
}
uint64_t dpa_cntr_readerr_unsafe(struct fid_cntr *fid_cntr){
  dpa_fid_cntr* cntr = container_of(fid_cntr, dpa_fid_cntr, cntr);
  make_queue_progress(&cntr->progress, 0);
  return cntr->err;
}
int dpa_cntr_add_unsafe(struct fid_cntr *cntr, uint64_t value){
  container_of(cntr, dpa_fid_cntr, cntr)->counter += value;
  return FI_SUCCESS;
}
static void dpa_cntr_inc_unsafe(dpa_fid_cntr *cntr){
  cntr->counter++;
}
static void dpa_cntr_err_inc_unsafe(dpa_fid_cntr *cntr){
  cntr->err++;
}
int dpa_cntr_set_unsafe(struct fid_cntr *cntr, uint64_t value){
  container_of(cntr, dpa_fid_cntr, cntr)->counter = value;
  return FI_SUCCESS;
}


uint64_t dpa_cntr_read_safe(struct fid_cntr *fid_cntr){
  dpa_fid_cntr* cntr = container_of(fid_cntr, dpa_fid_cntr, cntr);
  make_queue_progress(&cntr->progress, 0);
  return atomic_get(&cntr->counter_atomic);
}
uint64_t dpa_cntr_readerr_safe(struct fid_cntr *fid_cntr){
  dpa_fid_cntr* cntr = container_of(fid_cntr, dpa_fid_cntr, cntr);
  make_queue_progress(&cntr->progress, 0);
  return atomic_get(&cntr->err_atomic);
}
int dpa_cntr_add_safe(struct fid_cntr *cntr, uint64_t value){
  atomic_add(&container_of(cntr, dpa_fid_cntr, cntr)->counter_atomic,value);
  return FI_SUCCESS;
}
static void dpa_cntr_inc_safe(dpa_fid_cntr *cntr){
  atomic_inc(&cntr->counter_atomic);
}
static void dpa_cntr_err_inc_safe(dpa_fid_cntr *cntr){
  atomic_inc(&cntr->err_atomic);
}
int dpa_cntr_set_safe(struct fid_cntr *cntr, uint64_t value){
  atomic_set(&container_of(cntr, dpa_fid_cntr, cntr)->counter_atomic, value);
  return FI_SUCCESS;
}


int dpa_cntr_wait(struct fid_cntr *fid_cntr, uint64_t threshold, int timeout) {
  dpa_fid_cntr* cntr = container_of(fid_cntr, dpa_fid_cntr, cntr);
  struct fi_ops_cntr* ops = cntr->cntr.ops;
  int err = ops->readerr(fid_cntr);
  while(timeout > 0 && ops->read(fid_cntr) < threshold
        && ops->readerr(fid_cntr) == err)
    timeout = make_queue_progress(&cntr->progress, timeout);
  return (timeout == 0 && cntr->counter < threshold)
    ? -FI_ETIMEDOUT : 0;
}
