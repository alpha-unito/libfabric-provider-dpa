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
#include "dpa_env.h"
#include "dpa_msg.h"
#include "dpa_mr.h"
#include "dpa_info.h"

static int dpa_fabric(struct fi_fabric_attr *attr, struct fid_fabric **fabric, void *context);
static int dpa_fabric_close(fid_t fid);
static void fi_dpa_fini(void);

dpa_nodeid_t localNodeId;
#ifndef ADAPTERNO_DEFAULT
#define ADAPTERNO_DEFAULT 0
#endif
DEFINE_ENV_CONST(dpa_adapterno_t, localAdapterNo, ADAPTERNO_DEFAULT);

struct fi_provider dpa_provider = {
  .name="dpa",
  .version=FI_VERSION(DPA_MAJOR_VERSION, DPA_MINOR_VERSION),
  .fi_version=FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
  .getinfo=dpa_getinfo,
  .fabric=dpa_fabric,
  .cleanup=fi_dpa_fini
};

extern fastlock_t msg_lock;

FI_EXT_INI{
  //start dpalib
  dpa_error_t error;
  DPA_DEBUG("Initialize DPALIB\n");
  DPAInitialize(NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAInitialize, return NULL);
  
  ENV_OVERRIDE_INT(localAdapterNo);
  DPA_DEBUG("Getting local node id\n");
  DPAGetLocalNodeId(localAdapterNo,
                    &localNodeId,
                    NO_FLAGS,
                    &error);
  DPALIB_CHECK_ERROR(DPAGetLocalNodeId, return NULL);
  DPA_DEBUG("Local node id = %d\n", localNodeId);
  dpa_mr_init();
  dpa_msg_init();
  dpa_cm_init();
  return &dpa_provider;
}

/**
 * Finalize dpa provider
 */
static void fi_dpa_fini(void)
{
  dpa_mr_fini();
  dpa_msg_fini();
  dpa_cm_fini();
  //finalize dpalib
  DPATerminate();
}

static struct fi_ops dpa_fab_fi_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_fabric_close,
  .bind = fi_no_bind,
  .control = fi_no_control,
  .ops_open = fi_no_ops_open
};

static struct fi_ops_fabric dpa_fab_ops = {
  .size = sizeof(struct fi_ops_fabric),
  .domain = dpa_domain_open,
  .passive_ep = dpa_passive_ep_open,
  .eq_open = dpa_eq_open,
  .wait_open = fi_no_wait_open
};
    
/**
 * Open dpa fabric domain
 */
struct fid_fabric fab = {
  .fid = {
    .fclass = FI_CLASS_FABRIC,
    .ops = &dpa_fab_fi_ops
  },
  .ops = &dpa_fab_ops,
};
static int dpa_fabric(struct fi_fabric_attr *attr, struct fid_fabric **fabric, void *context) {
  if (!attr) return -FI_ENODATA;
  if(strcmp(attr->name, DPA_FABRIC_NAME)) return -FI_ENODATA;

  fab.fid.context = context;

  *fabric = &fab;
  return FI_SUCCESS;
}


static int dpa_fabric_close(fid_t fid) {
}
