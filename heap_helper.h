//
// Created by micha on 28.10.2021.
//

#ifndef PROJECT1_HEAP_HELPER_H
#define PROJECT1_HEAP_HELPER_H
#include "heap.h"
int is_fragmented(memory_chunk_t * chunk);
void defragmentator();
void add_fence(void *buffer);
int is_fence_undamaged(void *buffer);
void fencing(memory_chunk_t *chunk);
memory_chunk_t* get_chunk_that_has_memblock(void* memblock);
memory_chunk_t* get_chunk_with_enough_memory(size_t size);
memory_chunk_t *get_last_chunk();
void initialize_first_chunk(size_t size, void *memory);
int are_fences_undamaged(const memory_chunk_t * chunk);
int round_beginning_address(size_t rounding_factor);
void set_free_chunks_size(memory_chunk_t *chunk);
size_t sum_size_of_chunks();
size_t get_absolute_chunk_size(memory_chunk_t *input);
size_t round_address(memory_chunk_t *temp);
size_t calculate_add_for_rounding(char *ptr);
memory_chunk_t *get_chunk_from_memblock(void *address);
int checksum(memory_chunk_t *chunk);
int insert_free_memory_block(memory_chunk_t *prev, memory_chunk_t *next);
int insert_memory_block(memory_chunk_t *prev, memory_chunk_t *next, size_t size);
memory_chunk_t *get_chunk_that_can_be_filled(size_t size);
#endif //PROJECT1_HEAP_HELPER_H
