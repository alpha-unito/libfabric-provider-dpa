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
#ifndef DPA_ENV_H
#define DPA_ENV_H

#include <stdlib.h>
#include <strings.h>

#ifdef DISABLE_ENV_OVERRIDE
#define DEFINE_ENV_CONST(type, var, value) const type var = value;
#define EXTERN_ENV_CONST(type, var) extern const type var;
#define ENV_OVERRIDE_INT(var) do { } while(0)
#else
#define DEFINE_ENV_CONST(type, var, value) type var = value;
#define EXTERN_ENV_CONST(type, var) extern type var;
#define ENV_OVERRIDE_INT(var) do {                          \
    char *s = getenv("FI_DPA_" #var);                                 \
    if (s) {                                                \
      if (s[0]>='0' && s[0]<='9')                           \
        var = (unsigned) atoi(s);                           \
      if (!strcasecmp(s, "yes") || !strcasecmp(s, "on"))    \
        var = 1;                                            \
      if (!strcasecmp(s, "no") || !strcasecmp(s, "off"))    \
        var = 0;                                            \
    }                                                       \
  } while(0)
#endif

#endif
