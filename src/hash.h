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
#include <stddef.h>
#include <stdlib.h>

#include "list.h"
#include "locks.h"
#include <rdma/fabric.h>

typedef struct hash_entry {
  slist_entry entry;
  char value[0];
} hash_entry;

struct hash_map;
typedef unsigned int (*hash_func_t)(struct hash_map *map, void* key);
typedef struct hash_map {
  size_t key_offset;
  size_t key_length;
  size_t value_size;
  size_t map_size;
  hash_func_t hash_func;
  fastlock_t lock;
  slist entries[0];
} hash_map;

#define field_size(type, field) sizeof(((type *)0)->field)

static inline unsigned int uint_hash(hash_map *map, void* key){
  return *((unsigned int*) key) % map->map_size;
}

static inline hash_map* _private_hash_create(size_t key_offset, size_t key_length, size_t value_size, size_t map_size, hash_func_t hash_func) {
  size_t entry_size = sizeof(struct slist);
  hash_map* map = calloc(1, sizeof(struct hash_map) + map_size * entry_size);

  map->key_offset = key_offset;
  map->key_length = key_length;
  map->value_size = value_size;
  map->map_size = map_size;
  map->hash_func = hash_func != NULL ? hash_func : uint_hash;
  for (int i = 0; i < map_size; i++)
	slist_init(&(map->entries[i]));
  fastlock_init(&map->lock);
  
  return map;
}

#define hash_create(type, field, size, hash_func) \
  _private_hash_create(offsetof(type,field), field_size(type,field), sizeof(type), size, hash_func)

static inline int gethash(hash_map *map, void* key) {
  return map->hash_func(map, key);
}

static inline int getvaluehash(hash_map *map, void* value){
  return gethash(map, (value+map->key_offset));
}

static inline void hash_put(hash_map *map, void* value){
  THREADSAFE(&map->lock, ({
      int hash = getvaluehash(map, value);
      hash_entry* newentry = calloc(1,sizeof(struct hash_entry) + map->value_size);
      memcpy(&(newentry->value), value, map->value_size);
      slist_insert_tail(&(newentry->entry), &(map->entries[hash]));
      }));
}

static inline void* hash_get(hash_map *map, void* key){
  int hash = gethash(map, key);
  slist entries = map->entries[hash];
  for (slist_entry* entry = entries.head; entry != NULL; entry = entry->next){
	void *value = container_of(entry, struct hash_entry, entry)->value;
	if (!memcmp(value + map->key_offset, key, map->key_length))
	  return value;
  }
  return NULL;
}


static inline void hash_remove(hash_map *map, void* key){
  THREADSAFE(&map->lock, ({
      int hash = gethash(map, key);
      struct _key_map {
        hash_map* map;
        void* key;
      } km = {map, key};
  
      int match_key(struct slist_entry *item, const void *arg){
        struct _key_map *km = (struct _key_map*) arg;
        void *value = container_of(item, struct hash_entry, entry)->value;
        return !memcmp(value + km->map->key_offset, km->key, km->map->key_length);
      };
  
      slist_entry* entry = slist_remove_first_match_unsafe(&(map->entries[hash]), match_key, &km);
      if (entry)
        free(container_of(entry, hash_entry, entry));
      }));
}
  
static inline void hash_destroy(hash_map* map, element_destroyer destroyer) {
  for (int i = 0; i < map->map_size; i++) {
    slist_destroy(&map->entries[i], hash_entry, entry, destroyer);
  }
  fastlock_destroy(&map->lock);
  free(map);
}
