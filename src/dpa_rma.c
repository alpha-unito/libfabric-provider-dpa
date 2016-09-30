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
#define LOG_SUBSYS FI_LOG_EP_DATA
#include "dpa_rma.h"
#include "dpa_av.h"
#include "dpa_ep.h"
  
void cache_disconnect(remote_mr_cache* cache) {
  dpa_error_t error;
  if (cache->sequence) {
    DPARemoveSequence(cache->sequence, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPARemoveSequence, );
    cache->sequence = NULL;
  }
  if (cache->map) {
    DPAUnmapSegment(cache->map, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPAUnmapSegment, );
    cache->map = NULL;
  }
  if (cache->segment) {
    DPADisconnectSegment(cache->segment, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPADisconnectSegment, );
    cache->segment = NULL;
  }
  if (cache->sd) {
    DPAClose(cache->sd, NO_FLAGS, &error);
    DPALIB_CHECK_ERROR(DPAClose, );
    cache->sd = NULL;
  }
  cache->target.nodeId = cache->target.connectId = 0;
  cache->base = NULL;
  cache->len = 0;
}

dpa_error_t cache_connect(dpa_fid_ep* ep, dpa_addr_t target) {
  if (target.nodeId == ep->last_remote_mr.target.nodeId &&
      target.connectId == ep->last_remote_mr.target.connectId &&
      ep->last_remote_mr.base)
    return DPA_ERR_OK;

  if (ep->last_remote_mr.segment)
    cache_disconnect(&ep->last_remote_mr);

  dpa_error_t error = DPA_ERR_OK;
  DPA_DEBUG("Connecting and mapping segment %u on node %u for RMA\n",
            target.segmentId, target.nodeId);
  DPAOpen(&ep->last_remote_mr.sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAOpen, goto cache_connect_end);

  DPAConnectSegment(ep->last_remote_mr.sd, &ep->last_remote_mr.segment,
                    target.nodeId, target.connectId, localAdapterNo,
                    NULL, NULL, DPA_INFINITE_TIMEOUT, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAConnectSegment, goto cache_connect_end);

  ep->last_remote_mr.len = DPAGetRemoteSegmentSize(ep->last_remote_mr.segment);

  ep->last_remote_mr.base = DPAMapRemoteSegment(ep->last_remote_mr.segment,
                                                &ep->last_remote_mr.map,
                                                0, ep->last_remote_mr.len,
                                                NULL, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAMapRemoteSegment, goto cache_connect_end);
  DPACreateMapSequence(ep->last_remote_mr.map, &ep->last_remote_mr.sequence, DPA_FLAG_FAST_BARRIER, &error);
  DPALIB_CHECK_ERROR(DPACreateMapSequence, goto cache_connect_end);
  dpa_sequence_status_t status;
  do {
    status = DPAStartSequence(ep->last_remote_mr.sequence, NO_FLAGS, &error);
  } while (status != DPA_SEQ_OK);

  ep->last_remote_mr.target = target;
 cache_connect_end:
  if (error != DPA_ERR_OK) cache_disconnect(&ep->last_remote_mr);
  return error;
}

void cache_disconnect_interrupt(remote_mr_cache* cache) {
  if (!cache->interrupt) return;
  dpa_error_t nocheck;
  DPADisconnectInterrupt(cache->interrupt, NO_FLAGS, &nocheck);
  cache->interrupt = NULL;
  cache->interruptId = 0;
}

dpa_error_t cache_connect_interrupt(remote_mr_cache* cache, dpa_intid_t interruptId) {
  if (cache->interrupt) {
    if (cache->interruptId == interruptId)
      return DPA_ERR_OK;
    else
      cache_disconnect_interrupt(cache);
  }
  dpa_error_t error;
  DPAConnectInterrupt(cache->sd, &cache->interrupt, cache->target.nodeId,
                      localAdapterNo, interruptId, DPA_INFINITE_TIMEOUT,
                      NO_FLAGS, &error);
  if (error == DPA_ERR_OK)
    cache->interruptId = interruptId;
  return error;
}

void signal_interrupt(remote_mr_cache* cache, uint64_t data) {
  dpa_intid_t interruptId = (dpa_intid_t) data;
  if (interruptId != data) return;
  dpa_error_t error = cache_connect_interrupt(cache, interruptId);
  DPALIB_CHECK_ERROR(DPAConnectInterrupt, return);

  DPATriggerInterrupt(cache->interrupt, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPATriggerInterrupt, );
}

ssize_t acquire_target(dpa_fid_ep* ep, const struct fi_msg_rma* msg, dpa_addr_t* target) {  
  if (ep->connected) target->nodeId = ep->peer_addr.nodeId;
  else {
    size_t addrlen = sizeof(dpa_addr_t);
    dpa_av_lookup(&ep->av->av, msg->addr, target, &addrlen);
  }
  target->connectId = (dpa_intid_t) msg->rma_iov[0].key;
  if (target->connectId != msg->rma_iov[0].key)
    return -FI_EINVAL; //truncation occurred, invalid

  if (cache_connect(ep, *target) != DPA_ERR_OK) return -FI_EREMOTEIO;
  return FI_SUCCESS;
}

ssize_t dpa_read(struct fid_ep *ep, void *buf, size_t len, void *desc,
                 fi_addr_t src_addr, uint64_t addr, uint64_t key, void *context) {
  const struct iovec iov = {
    .iov_base = buf,
    .iov_len = len
  };
  return dpa_readv(ep, &iov, &desc, 1, src_addr, addr, key, context);
}
ssize_t dpa_readv(struct fid_ep *ep, const struct iovec *iov, void **desc,
                 size_t count, fi_addr_t src_addr, uint64_t addr, uint64_t key,
                  void *context){
  const struct fi_rma_iov rma_iov = {
    .addr = addr,
    .key = key
  };
  const struct fi_msg_rma msg = {
    .msg_iov = iov,
    .desc = desc,
    .iov_count = count,
    .addr = src_addr,
    .rma_iov = &rma_iov,
    .rma_iov_count = 1,
    .context = context,
    .data = 0
  };
  return dpa_readmsg(ep, &msg, NO_FLAGS);
}
    
ssize_t dpa_readmsg(struct fid_ep *ep, const struct fi_msg_rma *msg,
                    uint64_t flags) {
  if (!msg) return -FI_EINVAL;
  if (!msg->rma_iov || msg->rma_iov_count != 1) return -FI_EINVAL;

  dpa_fid_ep* ep_priv = container_of(ep, dpa_fid_ep, ep);

  dpa_addr_t target;
  ssize_t ret = acquire_target(ep_priv, msg, &target);
  if (ret) return ret;

  volatile void* base = ep_priv->last_remote_mr.base + msg->rma_iov[0].addr;
  volatile void* top = ep_priv->last_remote_mr.base + ep_priv->last_remote_mr.len;
  size_t copied = 0;
  for (int i = 0; i < msg->iov_count && top - base > copied; i++) {
    size_t copy = MIN(top - base - copied, msg->msg_iov[i].iov_len);
    memcpy(msg->msg_iov[i].iov_base, (void*)base + copied, copy);
    copied += copy;
  }

  if (ep_priv->read_cq) {
    struct fi_cq_err_entry cq_entry = {
      .op_context = msg->context,
      .flags = FI_RMA | FI_READ,
      .len = copied,
      .buf = msg->iov_count == 1 ? msg->msg_iov[0].iov_base : NULL,
      .data = msg->data,
      .err = FI_SUCCESS
    };
    cq_add_src(ep_priv->read_cq, &cq_entry, ep_priv->connected ? 0 : msg->addr);
  }
  if (ep_priv->read_cntr)
    dpa_cntr_inc(ep_priv->read_cntr);
  

  if (flags & FI_REMOTE_CQ_DATA) {
    signal_interrupt(&ep_priv->last_remote_mr, msg->data);
  }
  return FI_SUCCESS;
}

ssize_t dpa_write(struct fid_ep *ep, const void *buf, size_t len, void *desc,
                  fi_addr_t dest_addr, uint64_t addr, uint64_t key, void *context){
  const struct iovec iov = {
    .iov_base = (void*) buf,
    .iov_len = len
  };
  return dpa_writev(ep, &iov, &desc, 1, dest_addr, addr, key, context);
}
ssize_t dpa_writev(struct fid_ep *ep, const struct iovec *iov, void **desc,
                   size_t count, fi_addr_t dest_addr, uint64_t addr, uint64_t key,
                   void *context){
  const struct fi_rma_iov rma_iov = {
    .addr = addr,
    .key = key
  };
  const struct fi_msg_rma msg = {
    .msg_iov = iov,
    .desc = desc,
    .iov_count = count,
    .addr = dest_addr,
    .rma_iov = &rma_iov,
    .rma_iov_count = 1,
    .context = context,
    .data = 0
  };
  return dpa_writemsg(ep, &msg, NO_FLAGS);
}
ssize_t dpa_writedata(struct fid_ep *ep, const void *buf, size_t len, void *desc,
                      uint64_t data, fi_addr_t dest_addr, uint64_t addr, uint64_t key,
                      void *context) {
  const struct iovec iov = {
    .iov_base = (void*) buf,
    .iov_len = len
  };
  const struct fi_rma_iov rma_iov = {
    .addr = addr,
    .key = key
  };
  const struct fi_msg_rma msg = {
    .msg_iov = &iov,
    .desc = &desc,
    .iov_count = 1,
    .addr = dest_addr,
    .rma_iov = &rma_iov,
    .rma_iov_count = 1,
    .context = context,
    .data = data
  };
  return dpa_writemsg(ep, &msg, FI_REMOTE_CQ_DATA);
}
ssize_t dpa_writemsg(struct fid_ep *ep, const struct fi_msg_rma *msg,
                     uint64_t flags) {
  if (!msg) return -FI_EINVAL;
  
  if (!msg->rma_iov || msg->rma_iov_count != 1) return -FI_EINVAL;
  dpa_fid_ep* ep_priv = container_of(ep, dpa_fid_ep, ep);

  dpa_addr_t target;
  ssize_t ret = acquire_target(ep_priv, msg, &target);
  if (ret) return ret;

  volatile void* base = ep_priv->last_remote_mr.base + msg->rma_iov[0].addr;
  volatile void* top = ep_priv->last_remote_mr.base + ep_priv->last_remote_mr.len;
  size_t copied = 0;
  for (int i = 0; i < msg->iov_count && top - base > copied; i++) {
    size_t copy = MIN(top - base - copied, msg->msg_iov[i].iov_len);
    memcpy((void*)base + copied, msg->msg_iov[i].iov_base, copy);
    copied += copy;
  }
  size_t total_len = 0;
  for (int i = 0; i < msg->iov_count; i++)
    total_len += msg->msg_iov[i].iov_len;

  if (ep_priv->write_cq) {
    struct fi_cq_err_entry cq_entry = {
      .op_context = msg->context,
      .flags = FI_RMA | FI_WRITE,
      .len = copied,
      .buf = NULL,
      .data = msg->data,
      .olen = total_len - copied,
      .err = total_len == copied ? FI_SUCCESS : FI_ETOOSMALL
    };
    cq_add_src(ep_priv->write_cq, &cq_entry, ep_priv->connected ? 0 : msg->addr);
  }
  if (ep_priv->write_cntr)
    dpa_cntr_inc(ep_priv->write_cntr);

  if (flags & FI_REMOTE_CQ_DATA)
    signal_interrupt(&ep_priv->last_remote_mr, msg->data);
  else if (!(flags & FI_MORE))
    DPAFlush(ep_priv->last_remote_mr.sequence, DPA_FLAG_FLUSH_CPU_BUFFERS_ONLY);
  return FI_SUCCESS;
}
