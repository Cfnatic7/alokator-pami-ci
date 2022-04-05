//
// Created by micha on 28.10.2021.
//

#include "heap_helper.h"
#include <assert.h>
int is_fragmented(memory_chunk_t * chunk) {
    if (!chunk) return -1;//error
    if (chunk->next) {
        if (chunk->free && chunk->next->free) {
            return 1;
        }
    }
    return 0;
}
void defragmentator() {
    memory_chunk_t *temp = memory_manager.first_memory_chunk, *temp2;
    int is_defragmented = 0;
    while(temp) {
        is_defragmented = 0;
        if (is_fragmented(temp)) {
            is_defragmented = 1;
            temp2 = temp->next;
            temp->next = temp2->next;
            if(temp->next) {
                temp->next->prev = temp;
                set_free_chunks_size(temp);
                temp->next->mem_control = checksum(temp->next);
            }
            else {
                temp->size += temp2->size + sizeof(memory_chunk_t);
            }
            temp->mem_control = checksum(temp);
        }
        if (!is_defragmented) {
            temp = temp->next;
        }
    }
}
void add_fence(void *buffer) {
    if (!buffer) return;
    char *ptr = (char *) buffer;
    for (size_t i = 0; i < SINGLE_FENCE_LENGTH; i++) {
        ptr[i] = '#';
    }
}
int is_fence_undamaged(void *buffer) {
    if (!buffer) return 0;
    char *ptr = buffer;
    for (size_t i = 0; i < SINGLE_FENCE_LENGTH; i++) {
        if (ptr[i] != '#') return 0;
    }
    return 1;
}
int are_fences_undamaged(const memory_chunk_t * chunk) {
    if (!chunk) return -1;
    if (!is_fence_undamaged((void *) ( (char *)chunk + sizeof(memory_chunk_t)))) return 0;
    if (!is_fence_undamaged((void *) ( (char *)chunk + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH + chunk->size))) return 0;
    return 1;
}
memory_chunk_t* get_chunk_with_enough_memory(size_t size) {
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    while(temp) {
        if (temp->free && temp->size >= size) {
            return temp;
        }
        temp = temp->next;
    }
    return NULL;
}
void fencing(memory_chunk_t *chunk) {
    if (!chunk) return;
    add_fence((void *) ( (char *) chunk + sizeof(memory_chunk_t)));//adds fence right after a control structure
    add_fence( (void *) ( (char *) chunk + sizeof(memory_chunk_t) + chunk->size + SINGLE_FENCE_LENGTH));//adds fence right after a control structure
}
memory_chunk_t *get_last_chunk() {
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    if (!temp) return NULL;
    while(temp->next) {
        temp = temp->next;
    }
    return temp;
}
void initialize_first_chunk(size_t size, void *memory) {
    if (size < 1 || !memory) return;
    memory_manager.first_memory_chunk = memory;
    memory_manager.first_memory_chunk->size = size;
    memory_manager.first_memory_chunk->free = 0;
    memory_manager.first_memory_chunk->next = NULL;
    memory_manager.first_memory_chunk->prev = NULL;
    memory_manager.first_memory_chunk->mem_control = checksum(memory_manager.first_memory_chunk);
    fencing(memory_manager.first_memory_chunk);
}
memory_chunk_t* get_chunk_that_has_memblock(void* memblock) {
    if (!memblock) return NULL;
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    while(temp) {
        if ((void *) ((char *) memblock - SINGLE_FENCE_LENGTH - sizeof(memory_chunk_t)) == (void *) temp) return temp;
        temp = temp->next;
    }
    return NULL;
}
int round_beginning_address(size_t rounding_factor) {
    if (!memory_manager.is_initialized) return 1;
    if (((unsigned long)((char *) memory_manager.memory_start + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH) & (rounding_factor - 1)) == 0) return 0;
    unsigned long rest = (unsigned long) ((char *) memory_manager.memory_start + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH) % rounding_factor;
    size_t add = rounding_factor - rest;
    void *control = custom_sbrk(add);
    if (control == (void *) -1) return 1;
    memory_manager.rounding = add;
    return 0;
}
void set_free_chunks_size(memory_chunk_t *chunk) {
    if (!chunk) return;
    char *temp;
    if (chunk->next && chunk->free) {
        temp = ((char *) chunk + CHUNK_STRUCT_SIZE);
        chunk->size = (char *)chunk->next - (char *)temp;
        return;
    }
    else return;
}
size_t sum_size_of_chunks() {
    size_t sum = 0;
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    if (!temp) return 0;
    while(temp->next) {
        sum += get_absolute_chunk_size(temp);
        temp = temp->next;
    }
    if (temp->free) {
        sum += FREE_CHUNK_SIZE(temp->size);
    }
    else sum += CHUNK_SIZE(temp->size);
    return sum;
}
size_t get_absolute_chunk_size(memory_chunk_t *input) {
    if (!input || !input->next) return 0;
    return (char *) input->next - (char *) input;
}
size_t round_address(memory_chunk_t *temp) {
    if (((unsigned long)((char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH) & (PAGE_SIZE - 1)) == 0) return 0;
    unsigned long rest = (unsigned long) ((char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH) % PAGE_SIZE;
    size_t add = PAGE_SIZE - rest;
    return add;
}
size_t calculate_add_for_rounding(char *ptr) {
    size_t rest = (unsigned long)((char *) ptr + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH) % PAGE_SIZE;
    size_t add = PAGE_SIZE - rest;
    return add;
}
memory_chunk_t *get_chunk_from_memblock(void *address) {
    if (!address) return NULL;
    return (memory_chunk_t *) ((char *) address - SINGLE_FENCE_LENGTH - CHUNK_STRUCT_SIZE);
}
int checksum(memory_chunk_t *chunk) {
    int sum = 0;
    if (chunk) {
        sum += (unsigned long long) chunk->prev % 1000000;
        sum += (unsigned long long) chunk->next % 1000000;
        sum += chunk->size % 1000000;
        sum += chunk->free % 1000000;
    }
    return sum;
}
int insert_free_memory_block(memory_chunk_t *prev, memory_chunk_t *next) {
    if (!prev || !next) return 1;
    size_t gap = (char *)next - (char *) prev;
    size_t prev_size = CHUNK_SIZE(ALIGN(prev->size));
    if (gap <= prev_size + CHUNK_STRUCT_SIZE) return 1;
    memory_chunk_t *new = (memory_chunk_t *) ((char *) prev + prev_size);
    new->prev = prev;
    prev->next = new;
    new->next = next;
    next->prev = new;
    new->free = 1;
    set_free_chunks_size(new);
    prev->mem_control = checksum(prev);
    next->mem_control = checksum(next);
    new->mem_control = checksum(new);
    return 0;
}
int insert_memory_block(memory_chunk_t *prev, memory_chunk_t *next, size_t size) {
    if (!prev || !next) return 1;
    size_t gap = (char *)next - (char *) prev;
    size_t prev_size = CHUNK_SIZE(ALIGN(prev->size));
    if (gap < prev_size + CHUNK_STRUCT_SIZE) return 1;
    memory_chunk_t *new = (memory_chunk_t *) ((char *) prev + prev_size);
    new->prev = prev;
    prev->next = new;
    new->next = next;
    next->prev = new;
    new->free = 0;
    new->size = size;
    fencing(new);
    insert_free_memory_block(new, next);
    prev->mem_control = checksum(prev);
    next->mem_control = checksum(next);
    new->mem_control = checksum(new);
    fencing(new);
    return 0;
}
memory_chunk_t *get_chunk_that_can_be_filled(size_t size) {
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    if (!temp) return NULL;
    while(temp->next) {
        if (!temp->free && get_absolute_chunk_size(temp) - ALIGN(CHUNK_SIZE(temp->size)) >= ALIGN(CHUNK_SIZE(size))) {
            return temp;
        }
        temp = temp->next;
    }
    return NULL;
}


