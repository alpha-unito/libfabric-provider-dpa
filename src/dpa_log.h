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
#ifndef LOG_SUBSYS
#define LOG_SUBSYS FI_LOG_CORE
#endif

#ifndef LOG_PROVIDER
#define LOG_PROVIDER &dpa_provider
#endif

#define DPA_LOG(LOG_FUNC, ...) LOG_FUNC(LOG_PROVIDER, LOG_SUBSYS, __VA_ARGS__)
#define DPA_DEBUG(...) DPA_LOG(FI_DBG, __VA_ARGS__)
#define DPA_TRACE(...) DPA_LOG(FI_TRACE, __VA_ARGS__)
#define DPA_INFO(...) DPA_LOG(FI_INFO, __VA_ARGS__)
#define DPA_WARN(...) DPA_LOG(FI_WARN, __VA_ARGS__)

#define VERIFY_FAIL_SPEC(param, sup, type) do {                         \
	DPA_INFO(#param "=" type ", supported=" type "\n", param, sup);     \
	return -FI_ENODATA;                                                 \
  } while (0)
  
#define VERIFY_FAIL(param, sup) VERIFY_FAIL_SPEC(param, sup, "%d")

#define DPALIB_CHECK_ERROR(func, op)                        \
  if (error != DPA_ERR_OK) {								\
	DPA_WARN(#func " failed, Error code 0x%x\n",error);		\
	do { op; } while (0);                                   \
  }
