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
#define LOG_SUBSYS FI_LOG_AV
#include "dpa.h"
#include "dpa_av.h"
#include "array.h"

static int dpa_av_close(fid_t fid);
static int dpa_av_insert(struct fid_av *av, const void *addr, size_t count,
                         fi_addr_t *fi_addr, uint64_t flags, void *context);
static int dpa_av_remove(struct fid_av *av, fi_addr_t *fi_addr,
                         size_t count, uint64_t flags);
static const char *dpa_av_straddr(struct fid_av *av, const void *addr,
                                  char *buf, size_t *len);

static struct fi_ops dpa_av_fi_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_av_close,
  .bind = fi_no_bind,
  .control = fi_no_control,
  .ops_open = fi_no_ops_open
};

static struct fi_ops_av dpa_av_ops = {
  .size = sizeof(struct fi_ops_av),
  .insert = dpa_av_insert,
  .insertsvc = fi_no_av_insertsvc,
  .insertsym = fi_no_av_insertsym,
  .remove = dpa_av_remove,
  .lookup = dpa_av_lookup,
  .straddr = dpa_av_straddr
};

int dpa_av_open(struct fid_domain *domain, struct fi_av_attr *attr,
                struct fid_av **av, void *context){
  enum fi_av_type type = FI_AV_MAP;
  size_t count = 0;
  if (attr) {
    if (attr->flags) VERIFY_FAIL(attr->flags, 0);
    if (attr->type == FI_AV_TABLE)
      type = FI_AV_TABLE;
    count = attr->count;
  }
  
  dpa_fid_av* av_priv =  ALLOC_INIT(struct dpa_fid_av, {
      .av = {
        .fid = {
          .fclass = FI_CLASS_AV,
          .context = context,
          .ops = &dpa_av_fi_ops,
        },
        .ops = &dpa_av_ops,
      },
      .domain = container_of(domain, dpa_fid_domain, domain),
    });
  if (type == FI_AV_TABLE){
    av_priv->table = array_create(count, dpa_addr_t);
    av_priv->last = 0;
  }                                       

  *av = &(av_priv->av);
  
  return 0;
}

static int dpa_av_close(fid_t fid) {
  dpa_fid_av* av = container_of(fid, struct dpa_fid_av, av.fid);
  array_destroy(av->table);
  free(av);
  return 0;
}

static int check_table_size(dpa_fid_av* av_priv, size_t count){
  dpa_addr_t* newtable = array_request_size(av_priv->table, av_priv->last + count);
  if (!newtable) return -FI_ENOMEM;
  av_priv->table = newtable;
  return 0;
}

static int dpa_av_insert(struct fid_av *av, const void *addr, size_t count,
                         fi_addr_t *fi_addr, uint64_t flags, void *context){
  dpa_fid_av* av_priv = container_of(av, struct dpa_fid_av, av);
  if (check_table_size(av_priv, count)) return -FI_ENOMEM;

  memcpy(&av_priv->table[av_priv->last], addr, count * sizeof(dpa_addr_t));
  for (int i = 0; i < count; i++) {
    fi_addr[i] = _get_fi_addr(av_priv, av_priv->last +i);
  }  
  av_priv->last += count;
  return count;
}
static int dpa_av_remove(struct fid_av *av, fi_addr_t *fi_addr,
                         size_t count, uint64_t flags){
  return 0;
}

int dpa_av_lookup(struct fid_av *av, fi_addr_t fi_addr, void *addr,
                         size_t *addrlen) {
  if(!addr || !addrlen) return -FI_EINVAL;
  
  dpa_fid_av* av_priv = container_of(av, struct dpa_fid_av, av);
  dpa_addr_t* result;
  if (av_priv->type == FI_AV_TABLE) {
    if (fi_addr > av_priv->last) return -FI_EINVAL;
    result = &av_priv->table[fi_addr];
  }
  else result = (dpa_addr_t*) fi_addr;
  size_t len = MIN(*addrlen, sizeof(dpa_addr_t));
  memcpy(addr, result, len);
  *addrlen = sizeof(dpa_addr_t);
  return 0;
}

static const char *dpa_av_straddr(struct fid_av *av, const void *addr,
                                  char *buf, size_t *len) {
  if(!buf || !len) return NULL;

  int n = snprintf(buf, *len, "%lld", *((fi_addr_t*)addr));
  if (n < 0) return NULL;
  *len = n + 1;
  return buf;
}
