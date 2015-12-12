/* array.h --- 
 * Copyright (c) 2015 University of Torino, Italy */

/* This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 * 
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 * 
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 * 
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *  *
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
 *     Marco Aldinucci (UniTO-A3Cube CSO): code design supervision
 */
#ifndef _ARRAY_H
#define _ARRAY_H

typedef struct array {
  size_t value_size;
  size_t count;
  uint8_t entries[0];
} array;

static inline void* _array_create_priv(size_t count, size_t value_size) {
  array* arr = malloc(sizeof(array) + value_size * count);
  arr->value_size = value_size;
  arr->count = count;
  return &arr->entries;
}

#define array_create(count, type) (type*) _array_create_priv(count, sizeof(type))

static inline size_t array_count(void* a) {
  return container_of(a, array, entries)->count;
}

static inline void array_destroy(void* a) {
  free(container_of(a, array, entries));
}

static inline void* array_request_size(void* a, size_t count) {
  array* arr = container_of(a, array, entries);
  if (count <= arr->count) return a;
  size_t newcount = 2 * arr->count + count;
  arr = realloc(arr, sizeof(array) +
                newcount * sizeof(arr->value_size));
  if (!arr) return NULL;
  return &arr->entries;
}

#endif
