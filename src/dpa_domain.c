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
#include "dpa.h"
#include "dpa_ep.h"
#include "dpa_domain.h"
#include "dpa_cntr.h"

static struct fi_ops dpa_fid_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_domain_close,
  .bind = fi_no_bind,
  .control = fi_no_control,
  .ops_open = fi_no_ops_open
};

static struct fi_ops_domain dpa_domain_ops = {
  .size = sizeof(struct fi_ops_domain),
  .av_open = dpa_av_open,
  .cq_open = dpa_cq_open,
  .endpoint = dpa_ep_open,
  .cntr_open = dpa_cntr_open
};

static struct fi_ops_mr dpa_mr_ops = {
  .size = sizeof(struct fi_ops_mr),
  .reg = dpa_mr_reg,
};

int	dpa_domain_open(struct fid_fabric *fabric, struct fi_info *info, struct fid_domain **dom, void *context){
  if (!info) return -FI_ENODATA;

  DPA_DEBUG("Build domain\n");
  enum fi_progress control_progress = FI_PROGRESS_AUTO;
  enum fi_progress data_progress = FI_PROGRESS_MANUAL;
  enum fi_mr_mode mr_mode = FI_MR_SCALABLE;
  enum fi_threading threading = FI_THREAD_COMPLETION;
  if (info->domain_attr) {
    if (info->domain_attr->mr_mode == FI_MR_BASIC)
      VERIFY_FAIL(info->domain_attr->mr_mode, FI_MR_SCALABLE);
    DPA_DEBUG("Set non default domain options\n");
    if (info->domain_attr->data_progress == FI_PROGRESS_AUTO)
      data_progress = FI_PROGRESS_AUTO;
    if (info->domain_attr->threading != FI_THREAD_UNSPEC &&
        info->domain_attr->threading < FI_THREAD_COMPLETION)
      threading = info->domain_attr->threading;
  }

  struct dpa_fid_domain* result = ALLOC_INIT(struct dpa_fid_domain, {
    .domain = {
      .fid = {
        .fclass = FI_CLASS_DOMAIN,
        .context = context,
        .ops = &dpa_fid_ops,
      },
      .ops = &dpa_domain_ops,
      .mr = &dpa_mr_ops,
    },
    .mr_mode = mr_mode,
    .fabric = fabric,
    .control_progress = control_progress,
    .data_progress = data_progress,
    .threading = threading,
  });

  *dom = &(result->domain);
  return 0;
}

int dpa_domain_close(struct fid *fid){
}
