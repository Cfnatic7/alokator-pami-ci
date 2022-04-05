//
// Created by micha on single_fence_length(.10.2021.
//

#include "heap.h"
#include "heap_helper.h"
#include <string.h>
int heap_setup(void) {
    if (memory_manager.is_initialized) return -1;
    void *mem_start = custom_sbrk(0);
    if (mem_start == (void *) -1) return -1;
    memory_manager.memory_start = mem_start;
    memory_manager.is_initialized = 1;
    memory_manager.first_memory_chunk = NULL;
    memory_manager.memory_size = 0;
    memory_manager.rounding = 0;
    pthread_mutex_init(&lock, NULL);
//    pthread_mutex_init(&calloc_lock, NULL);
//    pthread_mutex_init(&heap_validate_lock, NULL);
//    pthread_mutex_init(&realloc_lock, NULL);
    return 0;
}
void heap_clean(void) {
    if (!memory_manager.is_initialized) return;
    memory_chunk_t *temp = memory_manager.first_memory_chunk, *temp_next;
    while(temp) {
        temp_next = temp->next;
        memset(temp,0, sizeof(memory_chunk_t));
        temp = temp_next;
    }
    custom_sbrk(-1 * (memory_manager.memory_size + memory_manager.rounding));
    memory_manager.is_initialized = 0;
    memory_manager.first_memory_chunk = NULL;
    memory_manager.memory_size = 0;
    memory_manager.memory_start = NULL;
    memory_manager.rounding = 0;
    pthread_mutex_destroy(&lock);
//    pthread_mutex_destroy(&calloc_lock);
//    pthread_mutex_destroy(&heap_validate_lock);
//    pthread_mutex_destroy(&realloc_lock);
}
void *heap_malloc(size_t size) {
    if (size == 0 || size > 65057756) {
//        pthread_mutex_unlock(&lock);
        return NULL;
    }
    if (!memory_manager.is_initialized) {
//        pthread_mutex_unlock(&lock);
        return NULL;
    }
    if (heap_validate()) {
        //pthread_mutex_unlock(&lock);
        return NULL;
    }
    pthread_mutex_lock(&lock);
    void *control;
    //adding the very first chunk
    if (memory_manager.first_memory_chunk == NULL) {
        round_beginning_address(WORD_SIZE);
        size_t add = ALIGN(size + sizeof(memory_chunk_t) + 2 * SINGLE_FENCE_LENGTH);
        control = custom_sbrk(add);
        if (control == (void *) -1 || !control) {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        memory_manager.memory_start = control;
        initialize_first_chunk(size, (void *)((char *)memory_manager.memory_start + memory_manager.rounding));
        memory_manager.memory_size = add;
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) memory_manager.first_memory_chunk + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    // this if checks whether you can fit a chunk before the first chunk due to sufficient rounding factor
    if (memory_manager.rounding >= ALIGN(CHUNK_SIZE(size))) {
        memory_chunk_t *new_first_chunk = (memory_chunk_t*)memory_manager.memory_start;
        new_first_chunk->prev = NULL;
        new_first_chunk->next = memory_manager.first_memory_chunk;
        memory_manager.first_memory_chunk->prev = new_first_chunk;
        new_first_chunk->size = size;
        new_first_chunk->free = 0;
        memory_manager.first_memory_chunk = new_first_chunk;
        insert_free_memory_block(memory_manager.first_memory_chunk, memory_manager.first_memory_chunk->next);
        memory_manager.first_memory_chunk->mem_control = checksum(memory_manager.first_memory_chunk);
        memory_manager.first_memory_chunk->next->mem_control = checksum(memory_manager.first_memory_chunk->next);
        fencing(memory_manager.first_memory_chunk);
        memory_manager.memory_size += memory_manager.rounding;
        memory_manager.rounding = 0;
        pthread_mutex_unlock(&lock);
        return (void *) ((char *)memory_manager.first_memory_chunk + CHUNK_STRUCT_SIZE + SINGLE_FENCE_LENGTH);
    }
    //finding free chunk with enough memory and allocating it
    memory_chunk_t *temp = get_chunk_with_enough_memory(ALIGN(size + 2 * SINGLE_FENCE_LENGTH)), *temp_prev;
    if (temp) {
        temp->free = 0;
        temp->size = size;
        fencing(temp);
        int check = insert_free_memory_block(temp, temp->next);
        if (check) temp->mem_control = checksum(temp);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    // filling the gaps
    temp = get_chunk_that_can_be_filled(size);
    if (temp) {
        insert_memory_block(temp, temp->next, size);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp->next + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    //adding new chunk at the end
    temp = get_last_chunk();
    if (temp) {
        size_t full_new_size = ALIGN(CHUNK_SIZE(size));
        control = custom_sbrk(full_new_size);
        if (control == (void *) -1 || !control) {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        memory_manager.memory_size += full_new_size;
        temp_prev = temp;
        if (temp->free) {
            temp = (memory_chunk_t *) ( (char *) temp +  ALIGN(FREE_CHUNK_SIZE(temp->size)));
        }
        else temp = (memory_chunk_t *) ( (char *) temp +  ALIGN(CHUNK_SIZE(temp->size)));
        temp->free = 0;
        temp->size = size;
        temp->prev = temp_prev;
        temp_prev->next = temp;
        temp->next = NULL;
        temp->mem_control = checksum(temp);
        temp->prev->mem_control = checksum(temp->prev);
        fencing(temp);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
void* heap_calloc(size_t number, size_t size) {
    if (number * size > 65057756) return NULL;
    if (!number || !size) {
        return NULL;
    }
    void *ptr = heap_malloc(number * size);
    if (!ptr) {
        return NULL;
    }
    memset(ptr, 0, number * size);
    return ptr;
}
void* heap_realloc(void* memblock, size_t size) {
    if(heap_validate()) {
        return NULL;
    }
    if (!memblock && size) {
        void *ptr = heap_malloc(size);
        return ptr;
    }
    if (get_pointer_type(memblock) != pointer_valid) {
        return NULL;
    }
    //freeing block if size is 0
    if (!size && memblock) {
        memory_chunk_t *temp = get_chunk_from_memblock(memblock);
        shortened_heap_free(temp);
//        pthread_mutex_unlock(&realloc_lock);
        return NULL;
    }
    pthread_mutex_lock(&lock);
    void *control;
    memory_chunk_t *temp = get_chunk_from_memblock(memblock), *temp2, *temp3;
    size_t control_size;
    // we stay in the same chunk
    if (!temp)  {
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    if (temp->size == size)  {
        pthread_mutex_unlock(&lock);
        return memblock;
    }
    if (temp->size > size) {
        temp->size = size;
        fencing(temp);
        int check = insert_free_memory_block(temp, temp->next);
        if (check) temp->mem_control = checksum(temp);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    // checks if next chunk is free and if there is enough memory in both chunks to fit new size so that we can expand first one and shrink the other
    if (temp->next) {
        control_size = (unsigned long)((char *) temp->next - (char *) temp - UNUSED_FIELDS_LENGTH + CHUNK_STRUCT_SIZE);//calculates real size of chunk
        size_t debug = control_size + temp->next->size;
        if (temp->next->free && debug >= size) {
            temp->next = temp->next->next;
            if (temp->next) {
                temp->next->prev = temp;
            }
            temp->size = size;
            int check = insert_free_memory_block(temp, temp->next);
            if (check) {
                temp->mem_control = checksum(temp);
                if (temp->next) {
                    temp->next->mem_control = checksum(temp->next);
                }
            }
            fencing(temp);
            pthread_mutex_unlock(&lock);
            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
        }
    }
    // if it's the last chunk we expand its size
    if (!temp->next) {
        size_t add = ALIGN(size) - ALIGN(temp->size);
        control = custom_sbrk(add);
        if (control == (void *) -1 || !control)  {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        memory_manager.memory_size += add /*+ 2 * SINGLE_FENCE_LENGTH*/;
        temp->size = size;
        fencing(temp);
        temp->mem_control = checksum(temp);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    // adding new chunk at the end
    temp = get_last_chunk();
    if (temp) {
        size_t new_add = ALIGN(CHUNK_SIZE(size));
        control = custom_sbrk(new_add);
        if (control == (void *) -1 || !control)  {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        memory_manager.memory_size += new_add;
        temp3 = get_chunk_from_memblock(memblock);
        if (temp->free) {
            temp2 = (memory_chunk_t *) ( (char *) temp + ALIGN(FREE_CHUNK_SIZE(temp->size)));
        }
        else temp2 = (memory_chunk_t *) ( (char *) temp + ALIGN(CHUNK_SIZE(temp->size)));
        temp2->prev = temp;
        temp->next = temp2;
        temp2->next = NULL;
        temp2->size = size;
        temp2->free = 0;
        temp2->mem_control = 0;
        memmove((void *) ((char*) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), (void *) ((char *) temp3 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), temp3->size);
        fencing(temp2);
        temp3->free = 1;
        set_free_chunks_size(temp3);
        temp->mem_control = checksum(temp);
        temp2->mem_control = checksum(temp2);
        temp3->mem_control = checksum(temp3);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
void heap_free(void *address) {
    pthread_mutex_lock(&lock);
    if (!address)  {
        pthread_mutex_unlock(&lock);
        return;
    }
    if (memory_manager.first_memory_chunk && !memory_manager.first_memory_chunk->next && (char *) address - SINGLE_FENCE_LENGTH - CHUNK_STRUCT_SIZE == (char *) memory_manager.first_memory_chunk) {
        memory_manager.first_memory_chunk->free = 1;
        memory_manager.first_memory_chunk->size = memory_manager.memory_size - CHUNK_STRUCT_SIZE;
        memory_manager.first_memory_chunk->mem_control = checksum(memory_manager.first_memory_chunk);
        pthread_mutex_unlock(&lock);
        return;
    }
    memory_chunk_t *temp = get_chunk_that_has_memblock(address);
    if (temp && !temp->free) {
        temp->free = 1;
        set_free_chunks_size(temp);
        temp->mem_control = checksum(temp);
    }
    defragmentator();
    pthread_mutex_unlock(&lock);
}
int heap_validate(void) {
    pthread_mutex_lock(&lock);
    if (!memory_manager.is_initialized) {
        pthread_mutex_unlock(&lock);
        return 2;
    }
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    if (!temp) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    while(temp) {
        if (temp->mem_control != checksum(temp)) {
            pthread_mutex_unlock(&lock);
            return 3;
        }
        if (!temp->free) {
            if (!are_fences_undamaged(temp)) {
                pthread_mutex_unlock(&lock);
                return 1;
            }
        }
        temp = temp->next;
    }
    pthread_mutex_unlock(&lock);
    return 0;
}
size_t heap_get_largest_used_block_size(void) {
    if (heap_validate()) return 0;
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    if (!temp) {
        return 0;
    }
    size_t size = 0;
    while(temp) {
        if (temp->size > size && !temp->free) size = temp->size;
        temp = temp->next;
    }
    return size;
}
enum pointer_type_t get_pointer_type(const void* const pointer) {
    if (!pointer) return pointer_null;
    if (heap_validate()) return pointer_heap_corrupted;
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    while(temp){
        if (temp->next) {
            if (!temp->free) {
                if ((char *) pointer >= (char *) temp + CHUNK_SIZE(temp->size) && (char *) pointer < (char*) temp->next) return pointer_unallocated;
            }
        }
        if ((char *) pointer < (char *) memory_manager.first_memory_chunk) return pointer_unallocated;
        if ((char *) pointer >= (char *) temp && (char *) pointer < (char *) temp + sizeof(memory_chunk_t))
            return pointer_control_block;
        if (!temp->free) {
            if ((((char *) pointer >= (char *) temp + sizeof(memory_chunk_t) &&
                  (char *) pointer < (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH) ||
                 ((char *) pointer >= (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH + temp->size &&
                  (char *) pointer < (char *) temp + CHUNK_SIZE(temp->size))))
                return pointer_inside_fences;
            if ((char *) pointer > (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH &&
                (char *) pointer < (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH + temp->size)
                return pointer_inside_data_block;
            if ((char *) pointer == (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH) return pointer_valid;
        }
        if (temp->free) {
            if ((char *) pointer >= (char *) temp + sizeof(memory_chunk_t) &&
                (char *) pointer < (char *) temp + FREE_CHUNK_SIZE(temp->size))
                return pointer_unallocated;
        }
        temp = temp->next;
    }
    return pointer_valid;
}
void* heap_malloc_aligned(size_t count) {
    if (count == 0 || count > 65057756)  {
        return NULL;
    }
    if (!memory_manager.is_initialized) {
        return NULL;
    }
    if (heap_validate()) {
        return NULL;
    }
    pthread_mutex_lock(&lock);
    void *control;
    size_t correct_size, rounding;
    //adding the very first chunk
    if (memory_manager.first_memory_chunk == NULL) {
        control = custom_sbrk(count + UNUSED_FIELDS_LENGTH);
        if (control == (void *) -1 || !control) {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        memory_manager.memory_start = control;
        round_beginning_address(PAGE_SIZE);
        initialize_first_chunk(count, (void *)((char *)memory_manager.memory_start + memory_manager.rounding));
        memory_manager.memory_size = count + UNUSED_FIELDS_LENGTH;
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) memory_manager.first_memory_chunk + CHUNK_STRUCT_SIZE + SINGLE_FENCE_LENGTH);
    }
    //finding free chunk with enough memory and allocating it
    memory_chunk_t *temp = get_chunk_with_enough_memory(count + 2 * SINGLE_FENCE_LENGTH), *temp_ret;
    rounding = round_address(temp);
    if (temp && temp->size >= rounding + count + 2 * SINGLE_FENCE_LENGTH) {
        memmove((void *) ((char *) temp + rounding), (void *) temp, CHUNK_STRUCT_SIZE);
        temp = (memory_chunk_t*) ((char *) temp + rounding);
//        }
        if (temp->next) {
            temp->next->prev = temp;
            temp->next->mem_control = checksum(temp->next);
        }
        if (temp->prev) {
            temp->prev->next = temp;
            temp->prev->mem_control = checksum(temp->prev);
        }
        temp->free = 0;
        temp->size = count;
        temp->mem_control = checksum(temp);
        fencing(temp);
//        insert_free_memory_block(temp->prev, temp);
//        insert_free_memory_block(temp, temp->next);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    //adding new chunk at the end
    temp = get_last_chunk();
    if (temp) {
        size_t debug_help;
        if (temp->free) {
            debug_help = calculate_add_for_rounding( (char *) temp + CHUNK_STRUCT_SIZE);
        }
        else debug_help = calculate_add_for_rounding((char *)temp + CHUNK_SIZE(temp->size));
        correct_size = debug_help + CHUNK_SIZE(count);
        control = custom_sbrk(correct_size);
        if (control == (void *) -1 || !control)  {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        memory_manager.memory_size += correct_size;
        if (temp->free) {
            temp_ret = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_STRUCT_SIZE);
        }
        else temp_ret = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_SIZE(temp->size));
        temp_ret->free = 0;
        temp_ret->size = count;
        temp_ret->prev = temp;
        temp->next = temp_ret;
        temp_ret->next = NULL;
        fencing(temp_ret);
        temp_ret->mem_control = checksum(temp_ret);
        set_free_chunks_size(temp);
        temp->mem_control = checksum(temp);
//        memory_manager.last_memory_chunk = temp_ret;
//        insert_free_memory_block(temp, temp->next);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp_ret + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
void* heap_calloc_aligned(size_t number, size_t size) {
    if (number * size > 65057756) return NULL;
    if (!number || !size) {
        return NULL;
    }
    void *ptr = heap_malloc_aligned(number * size);
    if (!ptr) {
        return NULL;
    }
    memset(ptr, 0, number * size);
    return ptr;
}
void* heap_realloc_aligned(void* memblock, size_t size) {
    if(heap_validate()) return NULL;
    if (!memblock && size){
        void *ptr = heap_malloc_aligned(size);
        return ptr;
    }
    if (get_pointer_type(memblock) != pointer_valid) {
        return NULL;
    }
    //freeing block if size is 0
    if (!size && memblock) {
        memory_chunk_t *temp = get_chunk_from_memblock(memblock);
        shortened_heap_free(temp);
//        pthread_mutex_unlock(&lock);
        return NULL;
    }
    pthread_mutex_lock(&lock);
    void *control;
    memory_chunk_t *temp = get_chunk_from_memblock(memblock), *temp2, *temp3;
    // we stay in the same chunk
    if (!temp) {
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    if (temp->size == size) {
        pthread_mutex_unlock(&lock);
        return memblock;
    }
    if (temp->size > size) {
        temp->size = size;
        temp->mem_control = checksum(temp);
        fencing(temp);
//        insert_free_memory_block(temp, temp->next);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
//    if (temp->next) {
//        //checks if you can still fit the new size even if chunk shows smaller chunk->size
//        size_t debug_size = (char*) temp->next - (char*) temp - UNUSED_FIELDS_LENGTH;
//        if ( debug_size >= size) {
//            temp->size = size;
//            temp->mem_control = checksum(temp);
//            fencing(temp);
//            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//        }
//    }
    //check neighbouring chunk for free memory and shrinks it so that the new size can fit in the current chunk
    if (temp->next) {
        size_t debug_size = (char *) temp->next - (char *) temp - UNUSED_FIELDS_LENGTH;
        if (debug_size + temp->next->size >= size) {
            temp2 = temp->next->next;
            temp->next = temp2;
            if (temp2) {
                temp2->prev = temp;
                temp2->mem_control = checksum(temp2);
            }
            temp->size = /*(char*) temp->next - (char *) temp - UNUSED_FIELDS_LENGTH*/ size;
            fencing(temp);
            temp->mem_control = checksum(temp);
//            insert_free_memory_block(temp, temp->next);
            pthread_mutex_unlock(&lock);
            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
        }
    }
    //these next two ifs check whether there is enough space in memory for the chunk
    if (!temp->free && !temp->next) {
        if (!temp->next && memory_manager.memory_size >= sum_size_of_chunks() + size - temp->size) {
            temp->size = size;
            fencing(temp);
            temp->mem_control = checksum(temp);
            pthread_mutex_unlock(&lock);
            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
        }
    }
    else if (temp->free && !temp->next) {
        if (!temp->next && memory_manager.memory_size >= sum_size_of_chunks() + size - temp->size + 2 * SINGLE_FENCE_LENGTH) {
            temp->size = size;
            fencing(temp);
            temp->mem_control = checksum(temp);
            pthread_mutex_unlock(&lock);
            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
        }
    }

    //if it's the last chunk, we expand its size
    if (!temp->next) {
        int debug = size /*+ 2 * SINGLE_FENCE_LENGTH*/ - temp->size;
//        control = custom_sbrk(calculate_number_of_pages(debug) * PAGE_SIZE);
        control = custom_sbrk(debug);
        if (control == (void *) -1 || !control) {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
//        memory_manager.memory_size += calculate_necessary_aligned_size(debug);
        memory_manager.memory_size += debug;
        temp->size = size;
        temp->mem_control = checksum(temp);
        fencing(temp);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    //we create new chunk and free memblock
    temp = get_last_chunk();
    if (temp) {
        size_t debug_help;
        if (temp->free) {
            debug_help = calculate_add_for_rounding( (char *) temp + CHUNK_STRUCT_SIZE);
        }
        else debug_help = calculate_add_for_rounding((char *)temp + CHUNK_SIZE(temp->size));
        size_t correct_size = debug_help + CHUNK_SIZE(size);
        control = custom_sbrk(correct_size);
        if (control == (void *) -1 || !control) {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        memory_manager.memory_size += correct_size;
        temp3 = get_chunk_from_memblock(memblock);
        if (temp->free) {
            temp2 = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_STRUCT_SIZE);
        }
        else temp2 = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_SIZE(temp->size));
        temp2->prev = temp;
        temp->next = temp2;
        temp2->next = NULL;
        temp2->size = size;
        temp2->free = 0;
        memmove((void *) ((char*) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), (void *) ((char *) temp3 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), temp3->size);
        fencing(temp2);
        temp3->free = 1;
        set_free_chunks_size(temp3);
        set_free_chunks_size(temp);
        temp->mem_control = checksum(temp);
        temp2->mem_control = checksum(temp2);
        temp3->mem_control = checksum(temp3);
//        insert_free_memory_block(temp, temp->next);
        pthread_mutex_unlock(&lock);
        return (void *) ( (char *) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
void* heap_malloc_debug(size_t count, int fileline, const char* filename) {
//    if (count == 0) {
////        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    if (!memory_manager.is_initialized) {
////        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    if (heap_validate()) {
//        //pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    pthread_mutex_lock(&lock);
//    void *control;
//    //adding the very first chunk
//    if (memory_manager.first_memory_chunk == NULL) {
//        round_beginning_address(WORD_SIZE);
//        size_t add = ALIGN(count + sizeof(memory_chunk_t) + 2 * SINGLE_FENCE_LENGTH);
//        control = custom_sbrk(add);
//        if (control == (void *) -1 || !control) {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
//        memory_manager.memory_start = control;
//        initialize_first_chunk(count, (void *)((char *)memory_manager.memory_start + memory_manager.rounding));
//        memory_manager.memory_size = add;
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) memory_manager.first_memory_chunk + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    // this if checks whether you can fit a chunk before the first chunk due to sufficient rounding factor
//    if (memory_manager.rounding >= ALIGN(CHUNK_SIZE(count))) {
//        memory_chunk_t *new_first_chunk = (memory_chunk_t*)memory_manager.memory_start;
//        new_first_chunk->prev = NULL;
//        new_first_chunk->next = memory_manager.first_memory_chunk;
//        memory_manager.first_memory_chunk->prev = new_first_chunk;
//        new_first_chunk->size = count;
//        new_first_chunk->free = 0;
//        memory_manager.first_memory_chunk = new_first_chunk;
//        insert_free_memory_block(memory_manager.first_memory_chunk, memory_manager.first_memory_chunk->next);
//        memory_manager.first_memory_chunk->mem_control = checksum(memory_manager.first_memory_chunk);
//        memory_manager.first_memory_chunk->next->mem_control = checksum(memory_manager.first_memory_chunk->next);
//        fencing(memory_manager.first_memory_chunk);
//        memory_manager.memory_size += memory_manager.rounding;
//        memory_manager.rounding = 0;
//        pthread_mutex_unlock(&lock);
//        return (void *) ((char *)memory_manager.first_memory_chunk + CHUNK_STRUCT_SIZE + SINGLE_FENCE_LENGTH);
//    }
//    //finding free chunk with enough memory and allocating it
//    memory_chunk_t *temp = get_chunk_with_enough_memory(ALIGN(count + 2 * SINGLE_FENCE_LENGTH)), *temp_prev;
//    if (temp) {
//        temp->free = 0;
//        temp->size = count;
//        fencing(temp);
//        int check = insert_free_memory_block(temp, temp->next);
//        if (check) temp->mem_control = checksum(temp);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    // filling the gaps
//    temp = get_chunk_that_can_be_filled(count);
//    if (temp) {
//        insert_memory_block(temp, temp->next, count);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp->next + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    //adding new chunk at the end
//    temp = get_last_chunk();
//    if (temp) {
//        size_t full_new_size = ALIGN(CHUNK_SIZE(count));
//        control = custom_sbrk(full_new_size);
//        if (control == (void *) -1 || !control) {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
//        memory_manager.memory_size += full_new_size;
//        temp_prev = temp;
//        if (temp->free) {
//            temp = (memory_chunk_t *) ( (char *) temp +  ALIGN(FREE_CHUNK_SIZE(temp->size)));
//        }
//        else temp = (memory_chunk_t *) ( (char *) temp +  ALIGN(CHUNK_SIZE(temp->size)));
//        temp->free = 0;
//        temp->size = count;
//        temp->prev = temp_prev;
//        temp_prev->next = temp;
//        temp->next = NULL;
//        temp->mem_control = checksum(temp);
//        temp->prev->mem_control = checksum(temp->prev);
//        fencing(temp);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    pthread_mutex_unlock(&lock);
//    return NULL;
    return NULL;
}
void* heap_calloc_debug(size_t number, size_t size, int fileline, const char* filename) {
//    if (!number || !size) {
//        return NULL;
//    }
//    void *ptr = heap_malloc_debug(number * size, fileline, filename);
//    if (!ptr) {
//        return NULL;
//    }
//    memset(ptr, 0, number * size);
//    printf("test line: %d || file: %s || current line: %d\n", fileline, filename, __LINE__);
//    fflush(stdout);
//    return ptr;
    return NULL;
}
void* heap_realloc_debug(void* memblock, size_t size, int fileline, const char* filename) {
//    if(heap_validate()) {
//        return NULL;
//    }
//    if (!memblock && size) {
//        void *ptr = heap_malloc(size);
//        return ptr;
//    }
//    if (get_pointer_type(memblock) != pointer_valid) {
//        return NULL;
//    }
//    //freeing block if size is 0
//    if (!size && memblock) {
//        memory_chunk_t *temp = get_chunk_from_memblock(memblock);
//        shortened_heap_free(temp);
////        pthread_mutex_unlock(&realloc_lock);
//        return NULL;
//    }
//    pthread_mutex_lock(&lock);
//    void *control;
//    memory_chunk_t *temp = get_chunk_from_memblock(memblock), *temp2, *temp3;
//    size_t control_size;
//    // we stay in the same chunk
//    if (!temp)  {
//        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    if (temp->size == size)  {
//        pthread_mutex_unlock(&lock);
//        return memblock;
//    }
//    if (temp->size > size) {
//        temp->size = size;
//        fencing(temp);
//        int check = insert_free_memory_block(temp, temp->next);
//        if (check) temp->mem_control = checksum(temp);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    // checks if next chunk is free and if there is enough memory in both chunks to fit new size so that we can expand first one and shrink the other
//    if (temp->next) {
//        control_size = (unsigned long)((char *) temp->next - (char *) temp - UNUSED_FIELDS_LENGTH + CHUNK_STRUCT_SIZE);//calculates real size of chunk
//        size_t debug = control_size + temp->next->size;
//        if (temp->next->free && debug >= size) {
//            temp->next = temp->next->next;
//            if (temp->next) {
//                temp->next->prev = temp;
//            }
//            temp->size = size;
//            int check = insert_free_memory_block(temp, temp->next);
//            if (check) {
//                temp->mem_control = checksum(temp);
//                if (temp->next) {
//                    temp->next->mem_control = checksum(temp->next);
//                }
//            }
//            fencing(temp);
//            pthread_mutex_unlock(&lock);
//            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//        }
//    }
//    // if it's the last chunk we expand its size
//    if (!temp->next) {
//        size_t add = ALIGN(size) - ALIGN(temp->size);
//        control = custom_sbrk(add);
//        if (control == (void *) -1 || !control)  {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
//        memory_manager.memory_size += add /*+ 2 * SINGLE_FENCE_LENGTH*/;
//        temp->size = size;
//        fencing(temp);
//        temp->mem_control = checksum(temp);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    // adding new chunk at the end
//    temp = get_last_chunk();
//    if (temp) {
//        size_t new_add = ALIGN(CHUNK_SIZE(size));
//        control = custom_sbrk(new_add);
//        if (control == (void *) -1 || !control)  {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
//        memory_manager.memory_size += new_add;
//        temp3 = get_chunk_from_memblock(memblock);
//        if (temp->free) {
//            temp2 = (memory_chunk_t *) ( (char *) temp + ALIGN(FREE_CHUNK_SIZE(temp->size)));
//        }
//        else temp2 = (memory_chunk_t *) ( (char *) temp + ALIGN(CHUNK_SIZE(temp->size)));
//        temp2->prev = temp;
//        temp->next = temp2;
//        temp2->next = NULL;
//        temp2->size = size;
//        temp2->free = 0;
//        temp2->mem_control = 0;
//        memmove((void *) ((char*) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), (void *) ((char *) temp3 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), temp3->size);
//        fencing(temp2);
//        temp3->free = 1;
//        set_free_chunks_size(temp3);
//        temp->mem_control = checksum(temp);
//        temp2->mem_control = checksum(temp2);
//        temp3->mem_control = checksum(temp3);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    pthread_mutex_unlock(&lock);
//    return NULL;
    return NULL;
}
void* heap_malloc_aligned_debug(size_t count, int fileline, const char* filename) {
//    pthread_mutex_lock(&lock);
//    if (count == 0)  {
//        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    if (!memory_manager.is_initialized) {
//        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    if (heap_validate()) {
//        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    void *control;
//    size_t correct_size, rounding;
//    //adding the very first chunk
//    if (memory_manager.first_memory_chunk == NULL) {
//        control = custom_sbrk(count + UNUSED_FIELDS_LENGTH);
//        if (control == (void *) -1 || !control) {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
//        memory_manager.memory_start = control;
//        round_beginning_address(PAGE_SIZE);
//        initialize_first_chunk(count, (void *)((char *)memory_manager.memory_start + memory_manager.rounding));
//        memory_manager.memory_size = count + UNUSED_FIELDS_LENGTH;
//        pthread_mutex_unlock(&lock);
//        printf("line: %d || file: %s || current line: %d\n", fileline, filename, __LINE__);
//        fflush(stdout);
//        return (void *) ( (char *) memory_manager.first_memory_chunk + CHUNK_STRUCT_SIZE + SINGLE_FENCE_LENGTH);
//    }
//    //finding free chunk with enough memory and allocating it
//    memory_chunk_t *temp = get_chunk_with_enough_memory(count + 2 * SINGLE_FENCE_LENGTH), *temp_ret;
//    rounding = round_address(temp);
//    if (temp && temp->size >= rounding + count + 2 * SINGLE_FENCE_LENGTH) {
//        memmove((void *) ((char *) temp + rounding), (void *) temp, CHUNK_STRUCT_SIZE);
//        temp = (memory_chunk_t*) ((char *) temp + rounding);
////        }
//        if (temp->next) {
//            temp->next->prev = temp;
//            temp->next->mem_control = checksum(temp->next);
//        }
//        if (temp->prev) {
//            temp->prev->next = temp;
//            temp->prev->mem_control = checksum(temp->prev);
//        }
//        temp->free = 0;
//        temp->size = count;
//        temp->mem_control = checksum(temp);
//        fencing(temp);
////        insert_free_memory_block(temp->prev, temp);
////        insert_free_memory_block(temp, temp->next);
//        pthread_mutex_unlock(&lock);
//        printf("line: %d || file: %s || current line: %d\n", fileline, filename, __LINE__);
//        fflush(stdout);
//        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    //adding new chunk at the end
//    temp = get_last_chunk();
//    if (temp) {
//        size_t debug_help;
//        if (temp->free) {
//            debug_help = calculate_add_for_rounding( (char *) temp + CHUNK_STRUCT_SIZE);
//        }
//        else debug_help = calculate_add_for_rounding((char *)temp + CHUNK_SIZE(temp->size));
//        correct_size = debug_help + CHUNK_SIZE(count);
//        control = custom_sbrk(correct_size);
//        if (control == (void *) -1 || !control)  {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
//        memory_manager.memory_size += correct_size;
//        if (temp->free) {
//            temp_ret = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_STRUCT_SIZE);
//        }
//        else temp_ret = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_SIZE(temp->size));
//        temp_ret->free = 0;
//        temp_ret->size = count;
//        temp_ret->prev = temp;
//        temp->next = temp_ret;
//        temp_ret->next = NULL;
//        fencing(temp_ret);
//        temp_ret->mem_control = checksum(temp_ret);
//        set_free_chunks_size(temp);
//        temp->mem_control = checksum(temp);
////        memory_manager.last_memory_chunk = temp_ret;
////        insert_free_memory_block(temp, temp->next);
//        pthread_mutex_unlock(&lock);
//        printf("line: %d || file: %s || current line: %d\n", fileline, filename, __LINE__);
//        fflush(stdout);
//        return (void *) ( (char *) temp_ret + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    pthread_mutex_unlock(&lock);
    return NULL;
}
void* heap_calloc_aligned_debug(size_t number, size_t size, int fileline, const char* filename) {
//    if (!number || !size) {
//        return NULL;
//    }
//    void *ptr = heap_malloc_aligned_debug(number * size, fileline, filename);
//    if (!ptr) {
//        return NULL;
//    }
//    memset(ptr, 0, number * size);
//    printf("test line: %d || file: %s || current line: %d\n", fileline, filename, __LINE__);
//    fflush(stdout);
//    return ptr;
    return NULL;
}
void* heap_realloc_aligned_debug(void* memblock, size_t size, int fileline, const char* filename) {
//    if(heap_validate()) return NULL;
//    if (!memblock && size){
//        void *ptr = heap_malloc_aligned(size);
//        return ptr;
//    }
//    if (get_pointer_type(memblock) != pointer_valid) {
//        return NULL;
//    }
//    //freeing block if size is 0
//    if (!size && memblock) {
//        memory_chunk_t *temp = get_chunk_from_memblock(memblock);
//        shortened_heap_free(temp);
////        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    pthread_mutex_lock(&lock);
//    void *control;
//    memory_chunk_t *temp = get_chunk_from_memblock(memblock), *temp2, *temp3;
//    // we stay in the same chunk
//    if (!temp) {
//        pthread_mutex_unlock(&lock);
//        return NULL;
//    }
//    if (temp->size == size) {
//        pthread_mutex_unlock(&lock);
//        return memblock;
//    }
//    if (temp->size > size) {
//        temp->size = size;
//        temp->mem_control = checksum(temp);
//        fencing(temp);
////        insert_free_memory_block(temp, temp->next);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
////    if (temp->next) {
////        //checks if you can still fit the new size even if chunk shows smaller chunk->size
////        size_t debug_size = (char*) temp->next - (char*) temp - UNUSED_FIELDS_LENGTH;
////        if ( debug_size >= size) {
////            temp->size = size;
////            temp->mem_control = checksum(temp);
////            fencing(temp);
////            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
////        }
////    }
//    //check neighbouring chunk for free memory and shrinks it so that the new size can fit in the current chunk
//    if (temp->next) {
//        size_t debug_size = (char *) temp->next - (char *) temp - UNUSED_FIELDS_LENGTH;
//        if (debug_size + temp->next->size >= size) {
//            temp2 = temp->next->next;
//            temp->next = temp2;
//            if (temp2) {
//                temp2->prev = temp;
//                temp2->mem_control = checksum(temp2);
//            }
//            temp->size = /*(char*) temp->next - (char *) temp - UNUSED_FIELDS_LENGTH*/ size;
//            fencing(temp);
//            temp->mem_control = checksum(temp);
////            insert_free_memory_block(temp, temp->next);
//            pthread_mutex_unlock(&lock);
//            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//        }
//    }
//    //these next two ifs check whether there is enough space in memory for the chunk
//    if (!temp->free && !temp->next) {
//        if (!temp->next && memory_manager.memory_size >= sum_size_of_chunks() + size - temp->size) {
//            temp->size = size;
//            fencing(temp);
//            temp->mem_control = checksum(temp);
//            pthread_mutex_unlock(&lock);
//            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//        }
//    }
//    else if (temp->free && !temp->next) {
//        if (!temp->next && memory_manager.memory_size >= sum_size_of_chunks() + size - temp->size + 2 * SINGLE_FENCE_LENGTH) {
//            temp->size = size;
//            fencing(temp);
//            temp->mem_control = checksum(temp);
//            pthread_mutex_unlock(&lock);
//            return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//        }
//    }
//
//    //if it's the last chunk, we expand its size
//    if (!temp->next) {
//        int debug = size /*+ 2 * SINGLE_FENCE_LENGTH*/ - temp->size;
////        control = custom_sbrk(calculate_number_of_pages(debug) * PAGE_SIZE);
//        control = custom_sbrk(debug);
//        if (control == (void *) -1 || !control) {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
////        memory_manager.memory_size += calculate_necessary_aligned_size(debug);
//        memory_manager.memory_size += debug;
//        temp->size = size;
//        temp->mem_control = checksum(temp);
//        fencing(temp);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    //we create new chunk and free memblock
//    temp = get_last_chunk();
//    if (temp) {
//        size_t debug_help;
//        if (temp->free) {
//            debug_help = calculate_add_for_rounding( (char *) temp + CHUNK_STRUCT_SIZE);
//        }
//        else debug_help = calculate_add_for_rounding((char *)temp + CHUNK_SIZE(temp->size));
//        size_t correct_size = debug_help + CHUNK_SIZE(size);
//        control = custom_sbrk(correct_size);
//        if (control == (void *) -1 || !control) {
//            pthread_mutex_unlock(&lock);
//            return NULL;
//        }
//        memory_manager.memory_size += correct_size;
//        temp3 = get_chunk_from_memblock(memblock);
//        if (temp->free) {
//            temp2 = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_STRUCT_SIZE);
//        }
//        else temp2 = (memory_chunk_t *) ( (char *) temp + debug_help + CHUNK_SIZE(temp->size));
//        temp2->prev = temp;
//        temp->next = temp2;
//        temp2->next = NULL;
//        temp2->size = size;
//        temp2->free = 0;
//        memmove((void *) ((char*) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), (void *) ((char *) temp3 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH), temp3->size);
//        fencing(temp2);
//        temp3->free = 1;
//        set_free_chunks_size(temp3);
//        set_free_chunks_size(temp);
//        temp->mem_control = checksum(temp);
//        temp2->mem_control = checksum(temp2);
//        temp3->mem_control = checksum(temp3);
////        insert_free_memory_block(temp, temp->next);
//        pthread_mutex_unlock(&lock);
//        return (void *) ( (char *) temp2 + sizeof(memory_chunk_t) + SINGLE_FENCE_LENGTH);
//    }
//    pthread_mutex_unlock(&lock);
    return NULL;
}
void print_chunk_data(memory_chunk_t *chunk) {
    if (!chunk) {
        printf("NULL\n");
        fflush(stdout);
        return;
    }
    printf("memcontrol: %d || free: %d || size: %zu || prev: %p || next %p || chunk %p\n", chunk->mem_control, chunk->free, chunk->size, (void *)chunk->prev, (void *)chunk->next, (void *)chunk);
    fflush(stdout);
}
void print_heap_data() {
    memory_chunk_t *temp = memory_manager.first_memory_chunk;
    while(temp) {
        print_chunk_data(temp);
        temp = temp->next;
    }
    printf("\n");
    fflush(stdout);
}
void print_memory_manager_data() {
    printf("Memory size: %lu, first chunk: %p, rounding: %lu\n", memory_manager.memory_size, (void *)memory_manager.first_memory_chunk, memory_manager.rounding);
    printf("\n");
    fflush(stdout);
}
void  shortened_heap_free(memory_chunk_t *chunk) {
    if (!chunk) return;
    pthread_mutex_lock(&lock);
    chunk->free = 1;
    set_free_chunks_size(chunk);
    chunk->mem_control = checksum(chunk);
    defragmentator();
    pthread_mutex_unlock(&lock);
}


