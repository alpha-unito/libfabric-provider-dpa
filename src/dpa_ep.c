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
#include "dpa.h"
#include "dpa_ep.h"
#include "dpa_av.h"
#include "dpa_cm.h"
#include "dpa_msg.h"
#include "dpa_rma.h"

static int dpa_ep_close(fid_t fid);
static int dpa_ep_control(struct fid *fid, int command, void *arg);
static int dpa_ep_bind(struct fid *fid, struct fid *bfid, uint64_t flags);
struct fi_ops dpa_ep_fid_ops = {
  .close = dpa_ep_close,
  .bind = dpa_ep_bind,
  .control = dpa_ep_control,
  .ops_open = fi_no_ops_open
};

struct fi_ops_ep dpa_ep_ops = {
  .size = sizeof(struct fi_ops_ep),
  .cancel = fi_no_cancel,
  .getopt = fi_no_getopt,
  .setopt = fi_no_setopt,
  .tx_ctx = fi_no_tx_ctx,
  .rx_ctx = fi_no_rx_ctx,
  .rx_size_left = fi_no_rx_size_left,
  .tx_size_left = fi_no_tx_size_left
};

static int dpa_getname(fid_t fid, void *addr, size_t *addrlen);
struct fi_ops_cm dpa_cm_ops = {
  .size = sizeof(struct fi_ops_cm),
  .getname = dpa_getname,
  .getpeer = fi_no_getpeer,
  .connect = dpa_connect,
  .listen = dpa_listen,
  .accept = dpa_accept,
  .reject = fi_no_reject,
  .shutdown = dpa_shutdown,
};

struct fi_ops_msg dpa_msg_ops = {
  .size = sizeof(struct fi_ops_msg),
  .recv = dpa_recv,
  .recvv = dpa_recvv,
  .recvmsg = dpa_recvmsg,
  .send = dpa_send,
  .sendv = dpa_sendv,
  .sendmsg = dpa_sendmsg,
  .inject = fi_no_msg_inject,
  .senddata = fi_no_msg_senddata,
  .injectdata = fi_no_msg_injectdata
};

struct fi_ops_rma dpa_rma_ops = {
  .size = sizeof(struct fi_ops_rma),
  .read = dpa_read,
  .readv = dpa_readv,
  .readmsg = dpa_readmsg,
  .write = dpa_write,
  .writev = dpa_writev,
  .writemsg = dpa_writemsg,
  .inject = fi_no_rma_inject,
  .writedata = dpa_writedata,
  .injectdata = fi_no_rma_injectdata
};

static inline int can_msg(uint64_t caps) {
  return caps & (FI_MSG);
}

static inline int can_rma(uint64_t caps) {
  return caps & (FI_RMA);
}

uint64_t check_ep_caps(uint64_t caps) {
  if ((caps & DPA_EP_MSG_CAP) != caps) {
    DPA_WARN("Unsupported capabilities. Use fi_getinfo to query about supported capabilities.\n");
    return 0;
  }
  /* Using FI_MSG and FI_RMA without specialization means all 
   * features of data transfer interface must be supported */
  if ((caps & DPA_MSG_CAP) == FI_MSG)
    caps |= DPA_MSG_CAP;
  if ((caps & DPA_RMA_CAP) == FI_RMA)
    caps |= DPA_RMA_CAP;
  return caps;
}
  

int dpa_ep_open(struct fid_domain *domain, struct fi_info *info,
                struct fid_ep **ep, void *context) {
  if (!info) return -FI_EINVAL;
  if(!info->dest_addr || info->dest_addrlen != sizeof(dpa_addr_t)) return -FI_EINVAL;
  uint64_t ep_caps = check_ep_caps(info->caps);
  if (!ep_caps) return -FI_EINVAL;

  dpa_fid_domain* domain_priv = container_of(domain, dpa_fid_domain, domain.fid);
  dpa_addr_t dest_addr = *((dpa_addr_t*)info->dest_addr);
  dpa_fid_ep* ep_priv = ALLOC_INIT(struct dpa_fid_ep, {
      .ep = {
        .fid = {
          .fclass = FI_CLASS_EP,
          .context = context,
          .ops = &dpa_ep_fid_ops,
        },
        .ops = &dpa_ep_ops,
        .cm = &dpa_cm_ops,
      },
      .domain = domain_priv,
      .peer_addr = dest_addr,
      .connected = 0,
      .lock_needed = domain_priv->threading < FI_THREAD_FID,
      .caps = ep_caps,
      .send_cq = NULL,
      .read_cq = NULL,
      .write_cq = NULL,
      .recv_cq = NULL,
      .send_cntr = NULL,
      .read_cntr = NULL,
      .write_cntr = NULL,
      .recv_cntr = NULL,
      .last_remote_mr = {
        .target = {
          .nodeId = 0,
          .segmentId = 0
        },
        .segment = NULL,
      }
    });
  
  if (can_msg(ep_caps)) {
    struct fi_ops_msg* ops = memdup(&dpa_msg_ops, sizeof(struct fi_ops_msg));
    if (!(ep_caps & FI_RECV)) {
      ops->recv = fi_no_msg_recv;
      ops->recvv = fi_no_msg_recvv;
      ops->recvmsg = fi_no_msg_recvmsg;
    }
    if (!(ep_caps & FI_SEND)) {
      ops->send = fi_no_msg_send;
      ops->sendv = fi_no_msg_sendv;
      ops->sendmsg = fi_no_msg_sendmsg;
    }
    ep_priv->ep.msg = ops;
  }
  if (can_rma(ep_caps)) {
    struct fi_ops_rma* ops = memdup(&dpa_rma_ops, sizeof(struct fi_ops_rma));
    if (!(ep_caps & FI_READ)) {
      ops->read = fi_no_rma_read;
      ops->readv = fi_no_rma_readv;
      ops->readmsg = fi_no_rma_readmsg;
    }
    if (!(ep_caps & FI_WRITE)) {
      ops->write = fi_no_rma_write;
      ops->writev = fi_no_rma_writev;
      ops->writemsg = fi_no_rma_writemsg;
      ops->writedata = fi_no_rma_writedata;
    }
    ep_priv->ep.rma = ops;
  }
    
  if (info->handle) {
    // opening active endpoint from passive
    ep_priv->pep = container_of(info->handle, dpa_fid_pep, pep.fid);
  }
  
  if (can_msg(ep_caps)) {
    slist_init(&ep_priv->msg_send_info.msg_queue);
    slist_init(&ep_priv->msg_recv_info.msg_queue);
    slist_init(&ep_priv->msg_send_info.free_entries);
    slist_init(&ep_priv->msg_recv_info.free_entries);
    slist_init(&ep_priv->free_entries_ptrs);
    create_msg_queue_entries(ep_priv, &ep_priv->msg_send_info.free_entries);
    create_msg_queue_entries(ep_priv, &ep_priv->msg_recv_info.free_entries);
  }
  *ep = &(ep_priv->ep);
  return 0;
}

static int dpa_pep_close(fid_t fid);
static int dpa_pep_bind(struct fid *fid, struct fid *bfid, uint64_t flags);
struct fi_ops dpa_pep_fid_ops = {
  .size = sizeof(struct fi_ops),
  .close = dpa_pep_close,
  .bind = dpa_pep_bind,
  .control = fi_no_control,
  .ops_open = fi_no_ops_open
};

int dpa_passive_ep_open(struct fid_fabric *fabric, struct fi_info *info,
                        struct fid_pep **pep, void *context){
  if (!info) return -FI_EINVAL;
  if(!info->src_addr || !info->src_addrlen == sizeof(dpa_addr_t)) return -FI_EINVAL;
  if (!check_ep_caps(info->caps)) return -FI_EINVAL;

  dpa_addr_t src_addr = *((dpa_addr_t*)info->src_addr);
  dpa_fid_pep* pep_priv = ALLOC_INIT(dpa_fid_pep, {
      .pep = {
        .fid = {
          .fclass = FI_CLASS_PEP,
          .context = context,
          .ops = &dpa_pep_fid_ops,
        },
        .ops = &dpa_ep_ops,
        .cm = &dpa_cm_ops
      },
      .fabric = fabric,
      .info = fi_dupinfo(info),
      .eq = NULL,
      .control_info = {
        .segmentId = src_addr.segmentId,
      }
    });
  fastlock_init(&pep_priv->lock);
  *pep = &pep_priv->pep;
  return 0;
}

static int dpa_ep_close(fid_t fid) {
  DPA_DEBUG("Closing endpoint\n");
  struct dpa_fid_ep *ep = container_of(fid, dpa_fid_ep, ep.fid);
  if (ep->ep.msg) {
    slist_destroy(&ep->free_entries_ptrs, msg_queue_ptr_entry, list_entry, no_destroyer);
  }
  free(ep);
  return 0;
}

static int dpa_pep_close(fid_t fid) {
  DPA_DEBUG("Closing endpoint\n");
  struct dpa_fid_pep *pep = container_of(fid, dpa_fid_pep, pep.fid);
  fastlock_destroy(&pep->lock);
  if (pep->eq)
    queue_progress_init(&pep->eq->progress);
  dpa_destroy_segment(pep->control_info);
  fi_freeinfo(pep->info);
  free(pep);
  return 0;
}

#define CHECK_DOMAIN(ep, bnd)                                           \
  if (ep->domain != bnd->domain){                                       \
    DPA_WARN("Binding" #bnd "to endpoint on a different domain\n");     \
    return -FI_EINVAL;                                                  \
  }

static inline void bind_progress(dpa_fid_ep* ep, queue_progress* progress, uint64_t flags) {
    if (ep->domain->data_progress != FI_PROGRESS_MANUAL)
      return;
    
    if ((flags | FI_RECV | FI_SEND) == flags)
      progress->func = (progress_queue_t) progress_sendrecv_queues;
    else if (flags & FI_RECV)
      progress->func = (progress_queue_t) progress_recv_queue;
    else if (flags & FI_SEND)
      progress->func = (progress_queue_t) progress_send_queue;
    progress->arg = ep;
}

static int dpa_ep_bind(struct fid *fid, struct fid *bfid, uint64_t flags){
  if (!bfid) return -FI_EINVAL;

  dpa_fid_ep* ep = container_of(fid, dpa_fid_ep, ep.fid);
  switch (bfid->fclass) {
  case FI_CLASS_EQ:
    DPA_DEBUG("Binding event queue to endpoint\n");
    dpa_fid_eq* eq = container_of(bfid, dpa_fid_eq, eq.fid);
    if (ep->domain->fabric != eq->fabric) {
      DPA_WARN("Binding eq to endpoint on a different fabric domain\n");
      return -FI_EINVAL;
    }
    ep->eq = eq;
    break;
  case FI_CLASS_CQ:
    DPA_DEBUG("Binding completion queue to endpoint\n");
    dpa_fid_cq* cq = container_of(bfid, dpa_fid_cq, cq.fid);
    CHECK_DOMAIN(ep, cq);
    bind_progress(ep, &cq->progress, flags);
    if (flags & FI_SEND) ep->send_cq = cq;
    if (flags & (FI_SEND | FI_READ)) ep->read_cq = cq;
    if (flags & (FI_SEND | FI_WRITE)) ep->write_cq = cq;
    if (flags & FI_RECV) ep->recv_cq = cq;
    break;
  case FI_CLASS_CNTR:
    DPA_DEBUG("Binding completion queue to endpoint\n");
    dpa_fid_cntr* cntr = container_of(bfid, dpa_fid_cntr, cntr.fid);
    CHECK_DOMAIN(ep, cntr);
    bind_progress(ep, &cntr->progress, flags);
    if (flags & FI_SEND) ep->send_cntr = cntr;
    if (flags & (FI_SEND | FI_READ)) ep->read_cntr = cntr;
    if (flags & (FI_SEND | FI_WRITE)) ep->write_cntr = cntr;
    if (flags & FI_RECV) ep->recv_cntr = cntr;
    break;
  case FI_CLASS_AV:
    DPA_DEBUG("Binding address vector to endpoint\n");
    dpa_fid_av* av = container_of(bfid, dpa_fid_av, av.fid);
    CHECK_DOMAIN(ep, av);
    ep->av = av;
    break;
  case FI_CLASS_MR:    
    DPA_DEBUG("Binding memory registration to endpoint\n");
    dpa_fid_mr* mr = container_of(bfid, dpa_fid_mr, mr.fid);
    CHECK_DOMAIN(ep, mr);
    ep->mr = mr;
    break;
  case FI_CLASS_STX_CTX:
    DPA_WARN("Binding stx_ctx to endpoint is not supported\n");
    return -FI_ENOSYS;
  default:
    DPA_WARN("Cannot bind bfid->fclass %d\n", bfid->fclass);
    return -FI_ENOSYS;
  }
  return 0;
}
  

static int dpa_pep_bind(struct fid *fid, struct fid *bfid, uint64_t flags){
  if (!bfid) return -FI_EINVAL;

  dpa_fid_pep* pep = container_of(fid, dpa_fid_pep, pep.fid);
  switch (bfid->fclass) {
  case FI_CLASS_EQ:
    DPA_DEBUG("Binding event queue to passive endpoint\n");
    dpa_fid_eq* eq = container_of(bfid, dpa_fid_eq, eq.fid);
    if (pep->fabric != eq->fabric) {
      DPA_WARN("Binding eq to endpoint on a different fabric domain\n");
      return -FI_EINVAL;
    }
    pep->eq = eq;
    break;
  default:
    DPA_WARN("Cannot bind bfid->fclass %d\n", bfid->fclass);
    return -FI_ENOSYS;    
  }
  return 0;
}

static int dpa_ep_control(struct fid *fid, int command, void *arg){
  switch (command) {
  case FI_ENABLE:
	DPA_DEBUG("Enabling endpoint\n");
	return 0;
  default:
	DPA_INFO("Unknown control operation %d\n", command);
	return -FI_ENOSYS;
  }
}

static int dpa_getname(fid_t fid, void *addr, size_t *addrlen) {
  dpa_fid_ep* ep = container_of(fid, dpa_fid_ep, ep.fid);
  dpa_addr_t local_addr = {
    .nodeId = localNodeId,
    .segmentId = (ep->pep) ? ep->pep->control_info.segmentId : 0
  };
  size_t copy_size = MIN(*addrlen, sizeof(dpa_addr_t));
  memcpy(addr, &local_addr, copy_size);
  *addrlen = sizeof(dpa_addr_t);
  return copy_size < sizeof(dpa_addr_t) ? -FI_ETOOSMALL : 0;
}

