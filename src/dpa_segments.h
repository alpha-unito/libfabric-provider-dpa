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
typedef struct local_segment_info local_segment_info;
#ifndef DPA_SEGMENTS_H
#define DPA_SEGMENTS_H

#include "dpa.h"
#include "fi_ext_dpa.h"

struct local_segment_info {
  dpa_desc_t sd;
  dpa_local_segment_t segment;
  dpa_segmid_t segmentId;
  dpa_map_t map;
  volatile void* base;
  size_t size;
};

typedef void (*segment_initializer)(local_segment_info* info);

static void zero_segment_initializer(local_segment_info* info) {
  memset((void*)info->base, 0, info->size);
}

static inline dpa_error_t dpa_alloc_segment(local_segment_info* info,
                                            dpa_segmid_t segmentId,
                                            size_t size,
                                            segment_initializer initializer,
                                            dpa_cb_local_segment_t segmentCallback,
                                            void* callbackArg){
  info->segmentId = segmentId;
  info->size = size;
  dpa_error_t error;
  DPA_DEBUG("Opening virtual device\n");
  DPAOpen(&info->sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAOpen, goto alloc_end);
  
  DPA_DEBUG("Creating segment %d\n", segmentId);
  unsigned int flags = segmentCallback ? DPA_FLAG_USE_CALLBACK : NO_FLAGS;
  DPACreateSegment(info->sd, &info->segment, segmentId, size, segmentCallback, callbackArg, flags, &error);
  DPALIB_CHECK_ERROR(DPACreateSegment, goto alloc_end);

  DPA_DEBUG("Preparing segment for DMA\n");
  DPAPrepareSegment(info->segment, localAdapterNo, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAPrepareSegment, goto alloc_end);

  DPA_DEBUG("Mapping segment\n");  
  info->base = DPAMapLocalSegment(info->segment, &info->map, 0, size, NULL, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAMapLocalSegment, goto alloc_end);

  if (initializer) {
    DPA_DEBUG("Initializing segment\n");
    initializer(info);
  }

  DPA_DEBUG("Making segment available for DMA\n");
  DPASetSegmentAvailable(info->segment, localAdapterNo, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPASetSegmentAvailable, goto alloc_end);
 alloc_end:
  return error;
}

static inline dpa_error_t dpa_destroy_segment(local_segment_info info) {
  dpa_error_t error;
  DPA_DEBUG("Making segment unavailable\n");
  DPASetSegmentUnavailable(info.segment, localAdapterNo, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPASetSegmentUnavailable,);

  DPA_DEBUG("Unmapping segment\n");
  DPAUnmapSegment(info.map, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAUnmapSegment,);

  DPA_DEBUG("Removing segment\n");
  DPARemoveSegment(info.segment, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPARemoveSegment,);

  DPA_DEBUG("Closing descriptor\n");
  DPAClose(info.sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAClose,);
  
  return error;
}

static inline dpa_sequence_t create_start_sequence(dpa_map_t map) {
  dpa_sequence_t sequence; dpa_error_t error;
  DPA_DEBUG("Creating sequence\n");
  DPACreateMapSequence(map, &sequence, DPA_FLAG_FAST_BARRIER, &error);
  DPALIB_CHECK_ERROR(DPACreateMapSequence, return NULL);
  dpa_sequence_status_t status;
  do {
    status = DPAStartSequence(sequence, NO_FLAGS, &error);
  } while (status != DPA_SEQ_OK);
  return sequence;
}

static inline dpa_error_t create_data_interrupt(dpa_desc_t* sd, dpa_local_data_interrupt_t* interrupt,
                                         dpa_intid_t* interruptId, unsigned int flags) {
  dpa_error_t error;
  DPAOpen(sd, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPAOpen, return error);
  DPACreateDataInterrupt(*sd, interrupt, localAdapterNo, interruptId,
                         NULL, NULL, flags, &error);
  DPALIB_CHECK_ERROR(DPACreateDataInterrupt, return error);
  return DPA_ERR_OK;
}

static inline void dpa_barrier(dpa_sequence_t sequence) {
  if (!sequence) return;
  DPAFlush(sequence, DPA_FLAG_FLUSH_CPU_BUFFERS_ONLY);
}

static inline void remove_sequence(dpa_sequence_t sequence) {
  if (!sequence) return;
  dpa_error_t error;
  DPA_DEBUG("Removing sequence\n");
  DPARemoveSequence(sequence, NO_FLAGS, &error);
  DPALIB_CHECK_ERROR(DPARemoveSequence, );
}
#endif
