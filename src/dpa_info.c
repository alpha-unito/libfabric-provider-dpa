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
#include <string.h>
#include "dpa.h"
static int get_ep_caps(struct fi_info* hints);
static int dpa_verify_requirements(uint32_t version, const char *node, const char *service,
                                   uint64_t flags, struct fi_info *hints);
static dpa_addr_t* resolvename(const char* node, const char *service);

#define FROMHINTS(field) ((hints && hints->field) ? hints->field : 0)
#define FROMHINTSPTR(field) ((hints && hints->field) \
                          ? memdup(hints->field, sizeof(*hints->field))  \
                          : calloc(1, sizeof(*hints->field)))

EXTERN_ENV_CONST(size_t, BUFFER_SIZE);
#include "dpa_msg_cm.h"
#define DPA_MAX_MSG_SIZE (ALIGNED_BUFFER_SIZE - offsetof(buffer_status, data))

/**
 * Get information about dpa provider capabilities
 */
int dpa_getinfo(uint32_t version, const char *node, const char *service,
                uint64_t flags, struct fi_info *hints, struct fi_info **info) {
  DPA_DEBUG("Check if dpa provider can satisfy all requirements\n");
  int ret = dpa_verify_requirements(version, node, service, flags, hints);
  if (ret) 
    return ret;

  dpa_addr_t *dest_addr = NULL;
  if (node && service && !(flags & FI_SOURCE))
    dest_addr = resolvename(node, service);
  else if (hints && hints->dest_addr && hints->dest_addrlen == sizeof(dpa_addr_t))
    dest_addr = (dpa_addr_t*) hints->dest_addr;

  dpa_addr_t *src_addr;
  if (service && (flags & FI_SOURCE))
    src_addr = resolvename(node, service);
  else if (hints && hints->src_addr && hints->src_addrlen == sizeof(dpa_addr_t))
    src_addr = hints->src_addr;
  else src_addr = calloc(1, sizeof(dpa_addr_t));
  src_addr->nodeId = localNodeId;

  enum fi_threading threading = FI_THREAD_COMPLETION;
  enum fi_progress control_progress = FI_PROGRESS_AUTO;
  enum fi_progress data_progress = FI_PROGRESS_MANUAL;
  enum fi_av_type av_type = FI_AV_UNSPEC;
  enum fi_mr_mode mr_mode = FI_MR_SCALABLE;
  if (hints && hints->domain_attr) {
    if (hints->domain_attr->data_progress == FI_PROGRESS_AUTO)
      data_progress = FI_PROGRESS_AUTO;
    if (hints->domain_attr->av_type != FI_AV_UNSPEC)
      av_type = hints->domain_attr->av_type;
    if (hints->domain_attr->threading != FI_THREAD_UNSPEC &&
        hints->domain_attr->threading < FI_THREAD_COMPLETION)
      threading = hints->domain_attr->threading;
  }
  struct fi_domain_attr* domain_attr = ALLOC_INIT(struct fi_domain_attr, {
        .domain = NULL,
          .name = strdup(DPA_DOMAIN_NAME),
          .threading = threading,
          .control_progress = control_progress,
          .data_progress = data_progress,
          .resource_mgmt = FI_RM_ENABLED,
          .av_type = av_type,
          .mr_mode = mr_mode,
          .mr_key_size = sizeof(dpa_segmid_t),
          .cq_data_size = 0,
          .cq_cnt = 0,
          .ep_cnt = 0,
          .tx_ctx_cnt = 1,
          .rx_ctx_cnt = 1,
          .max_ep_tx_ctx = 1,
          .max_ep_rx_ctx = 1,
          .max_ep_stx_ctx = 1,
          .max_ep_srx_ctx = 1
          });
  
  enum fi_ep_type ep_type = FI_EP_MSG;
  if (hints && hints->ep_attr){
    if (hints->ep_attr->type != FI_EP_UNSPEC)
      ep_type = hints->ep_attr->type;
  }
  struct fi_ep_attr* ep_attr = ALLOC_INIT(struct fi_ep_attr, {
        .type = ep_type,
          .protocol = FI_PROTO_DPA,
          .protocol_version = DPA_PROTO_VERSION,
          .max_msg_size = DPA_MAX_MSG_SIZE,
          .msg_prefix_size = DPA_PREFIX_SIZE,
          .max_order_raw_size = DPA_MAX_ORDER_SIZE,
          .max_order_war_size = DPA_MAX_ORDER_SIZE,
          .max_order_waw_size = DPA_MAX_ORDER_SIZE,
          .mem_tag_format = 0,
          .tx_ctx_cnt = DPA_MAX_CTX_CNT,
          .rx_ctx_cnt = DPA_MAX_CTX_CNT
          });

  struct fi_fabric_attr* fabric_attr = ALLOC_INIT(struct fi_fabric_attr, {
      .fabric = NULL,
        .name = strdup(DPA_FABRIC_NAME),
        .prov_name = DPA_PROVIDER_NAME,
        .prov_version = DPA_PROVIDER_VERSION
        });
  
  DPA_DEBUG("Building dpa_info\n");
  struct fi_info* result = ALLOC_INIT(struct fi_info, {
      .next = NULL,
        .caps = get_ep_caps(hints),
        .mode = 0,
        .addr_format = FI_FORMAT_UNSPEC,
        .src_addrlen = sizeof(dpa_addr_t),
        .dest_addrlen = dest_addr ? sizeof(dpa_addr_t) : 0,
        .src_addr = src_addr,
        .dest_addr = dest_addr,
        .handle = NULL,
        .tx_attr = FROMHINTSPTR(tx_attr),
        .rx_attr = FROMHINTSPTR(rx_attr),
        .ep_attr = ep_attr,
        .domain_attr = domain_attr,
        .fabric_attr = fabric_attr,
        });
  
  if (!result) {
	DPA_WARN("Unable to allocate memory for fi_info");
	return -FI_ENOMEM;
  }
  
  *info = result;
  return FI_SUCCESS;
}


dpa_addr_t* resolvename(const char* node, const char *service){
  DPA_DEBUG("Resolving node:%s service:%s\n", node, service);
  dpa_addr_t* result = calloc(1, sizeof(dpa_addr_t));
  
  if (node) {
    dis_nodeId_list_t nodeIdList;
    dis_adapter_type_t type;
    dpa_error_t error;
    DPAGetNodeIdByAdapterName((char *)node, &nodeIdList, &type, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPAGetNodeIdByAdapterName, return NULL);
    result->nodeId = nodeIdList[0];
  } else
    result->nodeId = 0;
  
  if (service)
    result->segmentId = atoi(service);
  else
    result->segmentId = 0;
  
  DPA_DEBUG("Resolved nodeId:%d segmentId:%d\n", result->nodeId, result->segmentId);
  return result;
}

static int dpa_verify_attr(struct fi_ep_attr *ep_attr, struct fi_tx_attr *tx_attr, struct fi_rx_attr *rx_attr);
/**
 * Check if dpa provider satisfies all requirements
 */
static int dpa_verify_requirements(uint32_t version, const char *node, const char *service,
                                   uint64_t flags, struct fi_info *hints) {
  int ret = FI_SUCCESS;

  DPA_DEBUG("check version\n");
  if (version > DPA_FI_VERSION)
	VERIFY_FAIL_SPEC(version, DPA_FI_VERSION);
  
  if (!hints)
	return FI_SUCCESS;

  DPA_DEBUG("check source address size\n");
  if (!(flags & FI_SOURCE) && hints->src_addr){
    if (hints->src_addrlen != sizeof(dpa_addr_t)) {
      VERIFY_FAIL(hints->src_addrlen, sizeof(dpa_addr_t));
    } else {
      DPA_DEBUG("check source address\n");
      dpa_addr_t* src_addr = (dpa_addr_t*) hints->src_addr;
      if (src_addr->nodeId != localNodeId)
        VERIFY_FAIL(src_addr->nodeId, localNodeId);
    }
  }

  DPA_DEBUG("check dest address size\n");
  if (((!node && !service) || (flags & FI_SOURCE)) &&
	  hints && hints->dest_addr &&
	  (hints->dest_addrlen != sizeof(dpa_addr_t)))
	VERIFY_FAIL(hints->dest_addrlen, sizeof(dpa_addr_t));
 

  DPA_DEBUG("check capabilities\n");
  int caps = get_ep_caps(hints);
  if (caps < 0) return caps;
  if ((caps | hints->caps) != caps)
	VERIFY_FAIL_SPEC(hints->caps, caps, "%x");

  DPA_DEBUG("check endpoint and context attributes\n");
  ret = dpa_verify_attr(hints->ep_attr, hints->tx_attr, hints->rx_attr);
  if (ret)
	return ret;

  DPA_DEBUG("check supported address format\n");
  switch (hints->addr_format) {
  case FI_FORMAT_UNSPEC:
	break;
  default:
	VERIFY_FAIL(hints->addr_format, FI_FORMAT_UNSPEC);
  }
  
  DPA_DEBUG("Check fabric provider name\n");
  if (hints->fabric_attr && hints->fabric_attr->prov_name &&
      strcmp(hints->fabric_attr->prov_name, DPA_PROVIDER_NAME))
    VERIFY_FAIL_SPEC(hints->fabric_attr->prov_name, DPA_PROVIDER_NAME, "%s");
  
  DPA_DEBUG("Check fabric name\n");
  if (hints->fabric_attr && hints->fabric_attr->name &&
      strcmp(hints->fabric_attr->name, DPA_FABRIC_NAME))
    VERIFY_FAIL_SPEC(hints->fabric_attr->name, DPA_FABRIC_NAME, "%s");

  DPA_DEBUG("Check mr mode\n");
  if (hints->domain_attr && hints->domain_attr->mr_mode == FI_MR_BASIC)
    VERIFY_FAIL(hints->domain_attr->mr_mode, FI_MR_SCALABLE);
  
  return FI_SUCCESS;
}

static int dpa_verify_rx_attr(struct fi_rx_attr *attr){
	if (!attr)
		return FI_SUCCESS;

	if ((attr->caps | DPA_EP_MSG_CAP) != DPA_EP_MSG_CAP)
		return -FI_ENODATA;
	
	return FI_SUCCESS;
}
static int dpa_verify_tx_attr(struct fi_tx_attr *attr){
	if (!attr)
		return FI_SUCCESS;

	if ((attr->caps | DPA_EP_MSG_CAP) != DPA_EP_MSG_CAP)
		return -FI_ENODATA;
	
	return FI_SUCCESS;
}

static int dpa_verify_ep_attr(struct fi_ep_attr *ep_attr){
  if (!ep_attr)
	return FI_SUCCESS;

  if (ep_attr->protocol != FI_PROTO_DPA && ep_attr->protocol != FI_PROTO_UNSPEC)
    VERIFY_FAIL(ep_attr->protocol, FI_PROTO_DPA);

  if (ep_attr->protocol_version > DPA_PROTO_VERSION)
	VERIFY_FAIL(ep_attr->protocol_version, DPA_PROTO_VERSION);

  if (ep_attr->max_msg_size > DPA_MAX_MSG_SIZE)
	VERIFY_FAIL(ep_attr->max_msg_size, DPA_MAX_MSG_SIZE);

  if (ep_attr->max_order_raw_size > DPA_MAX_ORDER_SIZE)
	VERIFY_FAIL(ep_attr->max_order_raw_size, DPA_MAX_ORDER_SIZE);

  if (ep_attr->max_order_war_size > DPA_MAX_ORDER_SIZE)
	VERIFY_FAIL(ep_attr->max_order_raw_size, DPA_MAX_ORDER_SIZE);

  if (ep_attr->max_order_waw_size > DPA_MAX_ORDER_SIZE)
	VERIFY_FAIL(ep_attr->max_order_waw_size, DPA_MAX_ORDER_SIZE);

  if (ep_attr->tx_ctx_cnt > DPA_MAX_CTX_CNT || ep_attr->tx_ctx_cnt == FI_SHARED_CONTEXT)
	VERIFY_FAIL(ep_attr->tx_ctx_cnt, DPA_MAX_CTX_CNT);

  if (ep_attr->rx_ctx_cnt > DPA_MAX_CTX_CNT || ep_attr->rx_ctx_cnt == FI_SHARED_CONTEXT)
	VERIFY_FAIL(ep_attr->tx_ctx_cnt, DPA_MAX_CTX_CNT);

  return FI_SUCCESS;
}


static int dpa_verify_attr(struct fi_ep_attr *ep_attr, struct fi_tx_attr *tx_attr, struct fi_rx_attr *rx_attr){
  if (dpa_verify_ep_attr(ep_attr) || dpa_verify_tx_attr(tx_attr) || dpa_verify_rx_attr(rx_attr))
	return -FI_ENODATA;
  return FI_SUCCESS;
}

static int get_ep_caps(struct fi_info* hints){
  if (!hints) return DPA_EP_MSG_CAP;
  enum fi_ep_type ep_type = hints->ep_attr ? hints->ep_attr->type : FI_EP_UNSPEC;
  int caps;
  switch (ep_type) {
  case FI_EP_UNSPEC:
  case FI_EP_MSG:
	caps = DPA_EP_MSG_CAP;
	break;
  case FI_EP_RDM:
  case FI_EP_DGRAM:
    caps = DPA_EP_RDM_CAP;
    break;
  default:
	VERIFY_FAIL(hints->ep_attr->type, FI_EP_MSG);
  }
  if ((caps | hints->caps) != caps)
	VERIFY_FAIL_SPEC(hints->caps, caps, "%x");
  return hints->caps ? hints->caps : caps;
}
  
  
