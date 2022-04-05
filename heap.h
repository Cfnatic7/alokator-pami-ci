//
// Created by micha on 16.10.2021.
//

#ifndef UNTITLED_HEAP_H
#define UNTITLED_HEAP_H
#define SINGLE_FENCE_LENGTH sizeof(void *)
#define CHUNK_STRUCT_SIZE 32
#define CHUNK_SIZE(size) (CHUNK_STRUCT_SIZE + 2 * SINGLE_FENCE_LENGTH + size)
#define FREE_CHUNK_SIZE(size) (CHUNK_STRUCT_SIZE + size)
#define UNUSED_FIELDS_LENGTH (CHUNK_STRUCT_SIZE + 2 * SINGLE_FENCE_LENGTH)
#define WORD_SIZE sizeof(void *)
#define ALIGN(x) (((x) + (WORD_SIZE - 1)) & ~(WORD_SIZE - 1))
#include <stdio.h>
#include <pthread.h>
pthread_mutex_t lock;
//pthread_mutex_t calloc_lock;
//pthread_mutex_t heap_validate_lock;
//pthread_mutex_t realloc_lock;
enum pointer_type_t {
    pointer_null,
    pointer_heap_corrupted,
    pointer_control_block,
    pointer_inside_fences,
    pointer_inside_data_block,
    pointer_unallocated,
    pointer_valid
};

struct memory_manager_t {
    void *memory_start;
    size_t memory_size;
    struct memory_chunk_t *first_memory_chunk;
    int is_initialized;
    size_t rounding;//for aligned family
};

struct memory_chunk_t {
    struct memory_chunk_t* prev;
    struct memory_chunk_t* next;
    size_t size;
    int free;
    int mem_control;
};
typedef struct memory_manager_t memory_manager_t;
typedef struct memory_chunk_t memory_chunk_t;
memory_manager_t memory_manager;
int heap_setup(void);
void heap_clean(void);
void* heap_malloc(size_t size);
void* heap_calloc(size_t number, size_t size);
void* heap_realloc(void* memblock, size_t size);
void  heap_free(void* memblock);
void  shortened_heap_free(memory_chunk_t *chunk);
void* heap_malloc_aligned(size_t count);
void* heap_calloc_aligned(size_t number, size_t size);
void* heap_realloc_aligned(void* memblock, size_t size);
void* heap_malloc_debug(size_t count, int fileline, const char* filename);
void* heap_calloc_debug(size_t number, size_t size, int fileline, const char* filename);
void* heap_realloc_debug(void* memblock, size_t size, int fileline, const char* filename);
void* heap_malloc_aligned_debug(size_t count, int fileline, const char* filename);
void* heap_calloc_aligned_debug(size_t number, size_t size, int fileline, const char* filename);
void* heap_realloc_aligned_debug(void* memblock, size_t size, int fileline, const char* filename);
size_t heap_get_largest_used_block_size(void);
enum pointer_type_t get_pointer_type(const void* const pointer);
int heap_validate(void);
void print_chunk_data(memory_chunk_t *chunk);
void print_heap_data();
void print_memory_manager_data();
#endif //UNTITLED_HEAP_H
