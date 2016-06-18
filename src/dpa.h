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
#ifndef DPA_H
#define DPA_H

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/uio.h>

#include <dpalib_api.h>

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_prov.h>
#include <rdma/fi_trigger.h>
#include <rdma/fi_log.h>

#define DPA_DOMAIN_NAME "dpa"
#define DPA_FABRIC_NAME "dpa"
#define DPA_PROVIDER_NAME "dpa"
#define DPA_PROVIDER_NAME_LEN 3
#define DPA_PROVIDER_VERSION 1

#define DPA_MAJOR_VERSION 1
#define DPA_MINOR_VERSION 3
#define DPA_FI_VERSION FI_VERSION(DPA_MAJOR_VERSION, DPA_MINOR_VERSION)

//in a provider specified protocol, the upper bit should be 1
#define FI_PROTO_DPA ((uint32_t) ((0x1 << 31) | 0x1))
#define DPA_PROTO_VERSION 1
#define DPA_PREFIX_SIZE 0
#define DPA_MAX_ORDER_SIZE SIZE_MAX
#define DPA_MAX_CTX_CNT 1

#define DPA_MSG_CAP (FI_MSG | FI_RECV | FI_SEND)
#define DPA_RMA_CAP (FI_RMA | FI_READ | FI_WRITE | FI_REMOTE_READ | FI_REMOTE_WRITE)
#define DPA_EP_MSG_CAP (DPA_MSG_CAP | DPA_RMA_CAP)
#define DPA_EP_RDM_CAP DPA_RMA_CAP

#define NO_FLAGS 0
#define NO_CALLBACK NULL


#include "fi_ext_dpa.h"
#include "dpa_env.h"
typedef struct dpa_addr_t dpa_addr_t;
typedef struct fi_dpa_ops_cq fi_dpa_ops_cq;

extern struct fi_provider dpa_provider;
EXTERN_ENV_CONST(dpa_adapterno_t, localAdapterNo);
extern dpa_nodeid_t localNodeId;

#include "dpa_log.h"
#include "dpa_utils.h"
#include "hash.h"
#include "list.h"
#include "enosys.h"

#endif
