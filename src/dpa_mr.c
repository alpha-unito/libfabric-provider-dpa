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
#include "dpa_mr.h"
#include "dpa_segments.h"
#include "dpa_env.h"

#ifndef MR_MAP_SIZE_DEFAULT
#define MR_MAP_SIZE_DEFAULT 251
#endif
DEFINE_ENV_CONST(size_t, MR_MAP_SIZE, MR_MAP_SIZE_DEFAULT);

static hash_map* mr_map = NULL;

void dpa_mr_init(){
  ENV_OVERRIDE_INT(MR_MAP_SIZE);
  mr_map = hash_create(dpa_fid_mr, segment_info.segmentId, MR_MAP_SIZE, NULL);
}

void dpa_mr_fini() {
  hash_destroy(mr_map, no_destroyer);
}

static int dpa_mr_close(struct fid *fid);
static int dpa_mr_bind(struct fid *fid, struct fid *bfid, uint64_t flags);

static struct fi_ops dpa_fi_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_mr_close,
  .bind = fi_no_bind,
  .control = fi_no_control,
  .ops_open = fi_no_ops_open
};


int dpa_mr_reg(struct fid *fid, const void *buf, size_t len,
               uint64_t access, uint64_t offset, uint64_t requested_key, uint64_t flags,
               struct fid_mr **mr, void *context) {  
  if (fid->fclass != FI_CLASS_DOMAIN)
    VERIFY_FAIL(fid->fclass, FI_CLASS_DOMAIN);
  dpa_fid_domain* domain_priv = container_of(fid, struct dpa_fid_domain, domain);
  if (domain_priv->mr_mode == FI_MR_BASIC)
    return -FI_EBADFLAGS;

  dpa_segmid_t segmentId = (dpa_segmid_t) requested_key;
  if (segmentId != requested_key)
    return -FI_EKEYREJECTED; // truncation occurred, so requested key cannot be used
  
  local_segment_info info;
  dpa_error_t error = dpa_alloc_segment(&info, segmentId, len, NULL, NULL, NULL);

  if (error == DPA_ERR_SEGMENTID_USED)
    return -FI_ENOKEY;
  else if (error != DPA_ERR_OK)
    return -FI_EOTHER;
  
  dpa_fid_mr* mr_priv = ALLOC_INIT(dpa_fid_mr, {
      .mr = {
        .fid = {
          .fclass = FI_CLASS_MR,
          .context = context,
          .ops = &dpa_fi_ops,
        },
        .mem_desc = (void*) info.base,
        .key = info.segmentId
      },
      .segment_info = info,
      .buf = buf,
      .len = len,
      .access = access,
      .flags = flags,
      .domain = domain_priv
    });

  hash_put(mr_map, mr_priv);
  
  *mr = &(mr_priv->mr);
  return 0;
}

static int dpa_mr_close(struct fid *fid){
  dpa_fid_mr *mr = container_of(fid, dpa_fid_mr, mr.fid);
  hash_remove(mr_map, &(mr->segment_info.segmentId));
  dpa_destroy_segment(mr->segment_info);
  free(mr);
  return 0;
}
