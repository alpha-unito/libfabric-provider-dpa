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
#define LOG_SUBSYS FI_LOG_EP_CTRL
#include "dpa_cm.h"
#include "dpa_ep.h"
#include "dpa_segments.h"
#include "dpa.h"
#include "dpa_msg.h"
#include "dpa_msg_cm.h"
#include "dpa_env.h"

EXTERN_ENV_CONST(dpa_segmid_t, MAX_MSG_SEGMID);
EXTERN_ENV_CONST(dpa_segmid_t, MIN_MSG_SEGMID);
EXTERN_ENV_CONST(size_t, BUFFERS_PER_SEGMENT);
EXTERN_ENV_CONST(size_t, BUFFER_SIZE);

#define NUM_SEGMENTS (MAX_MSG_SEGMID - MIN_MSG_SEGMID)
#define MAX_PEERS (NUM_SEGMENTS * BUFFERS_PER_SEGMENT)
#define CONTROL_SEGMENT_SIZE (MAX_PEERS*sizeof(struct control_data))

extern struct assign_data assign_data;
extern msg_local_segment_info* local_segments_info;
extern slist free_msg_queue_entries;

int progress_eq(dpa_fid_pep *pep, int timeout_millis);

int dpa_listen(struct fid_pep *pep) {
  dpa_fid_pep* pep_priv = container_of(pep, dpa_fid_pep, pep);
  if (!pep_priv->eq) return -FI_ENOEQ;
  DPA_DEBUG("Listening on segment %d\n", pep_priv->control_info.segmentId);
  dpa_error_t error =
    dpa_alloc_segment(&pep_priv->control_info, pep_priv->control_info.segmentId,
                      CONTROL_SEGMENT_SIZE, zero_segment_initializer,
                      NULL, NULL);
  pep_priv->eq->progress.func = (progress_queue_t) progress_eq;
  pep_priv->eq->progress.arg = pep_priv;
  if (error == DPA_ERR_SEGMENTID_USED)
    return -FI_EADDRINUSE;
  else if (error != DPA_ERR_OK)
    return -FI_EOTHER;
  
  return FI_SUCCESS;
}


int dpa_accept(struct fid_ep *ep, const void *param, size_t paramlen) {
  dpa_fid_ep* ep_priv = container_of(ep, dpa_fid_ep, ep);
  if (!(ep_priv->caps & FI_MSG)) return FI_SUCCESS;
  else return accept_msg(ep_priv);
}

int dpa_connect(struct fid_ep *ep, const void *addr,
                const void *param, size_t paramlen) {
  dpa_fid_ep* ep_priv = container_of(ep, dpa_fid_ep, ep);
  if (!(ep_priv->caps & FI_MSG)) return FI_SUCCESS;
  DPA_DEBUG("Connecting to endpoint %d:%d\n", ep_priv->peer_addr.nodeId, ep_priv->peer_addr.segmentId);
  segment_data remote_segment_data;
  dpa_error_t error = ctrl_connect_msg(ep_priv, &remote_segment_data);
  if (error != DPA_ERR_OK) return -FI_ECONNABORTED;
  error = connect_msg(ep_priv, remote_segment_data);
  if (error != DPA_ERR_OK) {
    disconnect_msg(ep_priv);
    return -FI_ECONNABORTED;
  }
  return FI_SUCCESS;
}

int dpa_shutdown(struct fid_ep *ep, uint64_t flags) {
  dpa_fid_ep* ep_priv = container_of(ep, dpa_fid_ep, ep);
  if (!(ep_priv->caps & FI_MSG)) return FI_SUCCESS;
  else return disconnect_msg(ep_priv) == DPA_ERR_OK ? FI_SUCCESS : -FI_EOTHER;
}

int dpa_cm_init(){
  ENV_OVERRIDE_INT(BUFFER_SIZE);
  ENV_OVERRIDE_INT(BUFFERS_PER_SEGMENT);
  ENV_OVERRIDE_INT(MIN_MSG_SEGMID);
  ENV_OVERRIDE_INT(MAX_MSG_SEGMID);

  fastlock_init(&assign_data.lock);
  THREADSAFE(&assign_data.lock, ({
        assign_data.currentSegmentId = MIN_MSG_SEGMID;
        local_segments_info = calloc(NUM_SEGMENTS, sizeof(msg_local_segment_info));
      }));
  return FI_SUCCESS;
}

int dpa_cm_fini() {
  //remove buffer infos for all peers
  for (int i = 0; i<NUM_SEGMENTS; i++) {
    local_segment_info info = local_segments_info[i].segment_info;
    if (info.segmentId) {
      //destroy data segment
      dpa_destroy_segment(info);
    }
    if (local_segments_info[i].buffers)
      free(local_segments_info[i].buffers);
  }
  free(local_segments_info);
  fastlock_destroy(&assign_data.lock);
  return FI_SUCCESS;
}
      
static inline void connection_request(dpa_fid_pep* pep, dpa_nodeid_t nodeId) {
  const struct fi_eq_cm_entry event = {
    .fid = &pep->pep.fid,
    .info = fi_dupinfo(pep->info),
  };
  event.info->dest_addr = ALLOC_INIT(dpa_addr_t, {
      .nodeId = nodeId,
      .segmentId = 0,
    });
  event.info->dest_addrlen = sizeof(dpa_addr_t);
  event.info->handle = &pep->pep.fid;
  eq_add(pep->eq, FI_CONNREQ, &event, sizeof(event), NO_FLAGS, 0);
}

int progress_eq(dpa_fid_pep *pep, int timeout_millis) {
  dpa_nodeid_t nodeId; dpa_adapterno_t adapterNo; dpa_error_t error;
  DPA_DEBUG("Awaiting connection on segment %d\n", pep->control_info.segmentId);
  timeout_millis = timeout_millis >= 0 ? timeout_millis : DPA_INFINITE_TIMEOUT;
  while(DPA_CB_CONNECT != DPAWaitForLocalSegmentEvent(pep->control_info.segment,
                                                      &nodeId, &adapterNo,
                                                      timeout_millis,
                                                      NO_FLAGS, &error)) {
    // avoid logging errors for timeout
    if (error == DPA_ERR_TIMEOUT) return 0;
    DPALIB_CHECK_ERROR(DPAWaitForLocalSegmentEvent, return 0);
  }
  connection_request(pep, nodeId);
  return timeout_millis;
}
