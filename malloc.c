#define USE_REAL_SBRK 0 // 0 for fake malloc, 1 for real malloc	
#pragma GCC diagnostic ignored "-Wunused-function"

#if USE_REAL_SBRK
#define _GNU_SOURCE

#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "malloc.h"

/* Function to allocate heap. Do not modify.
 * This is a wrapper around the system call sbrk.
 * For initialization, you can call this function as allocHeap(NULL, size)
 *   -> It will allocate a heap of size <size> bytes and return a pointer to the start address
 * For enlarging the heap, you can later call allocHeap(heapaddress, newsize)
 *   -> heapaddress must be the address previously returned by allocHeap(NULL, size)
 *   -> newsize is the new size
 *   -> function will return NULL (no more memory available) or heapaddress (if ok)
 */

uint8_t *allocHeap(uint8_t *currentHeap, uint64_t size)
{               
        static uint64_t heapSize = 0;
        if( currentHeap == NULL ) {
                uint8_t *newHeap  = sbrk(size);
                if(newHeap)
                        heapSize = size;
                return newHeap;
        }
	uint8_t *newstart = sbrk(size - heapSize);
	if(newstart == NULL) return NULL;
	heapSize += size;
	return currentHeap;
}
#else
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include "malloc.h"
// This is a "fake" version that you can use on MacOS
// sbrk as used above is not available on MacOS
// and normal malloc allocation does not easily allow resizing the allocated memory
uint8_t *allocHeap(uint8_t *currentHeap, uint64_t size)
{
        static uint64_t heapSize = 0;
        if( currentHeap == NULL ) {
                uint8_t *newHeap  = malloc(10*size);
                if(newHeap)
                        heapSize = 10*size;
                return newHeap;
        }
	if(size <= heapSize) return currentHeap;
        return NULL;
}
#endif


/*
 * This is the heap you should use.
 * (Initialized with a heap of size HEAP_SIZE) in initAllocator())
 */
uint8_t *_heapStart = NULL;
uint64_t _heapSize = 0;

/*
 * This should point to the first free block in memory.
 */
Block *_firstFreeBlock;

/*
 * Initializes the memory block. You don't need to change this.
 */
void initAllocator()
{
    _heapStart = allocHeap(NULL, HEAP_SIZE);
    _heapSize = HEAP_SIZE;
    
    // Initialize first free block
    _firstFreeBlock = (Block*)_heapStart;
    _firstFreeBlock->size = HEAP_SIZE;
    _firstFreeBlock->next = NULL;
}


/*
 * Gets the next block that should start after the current one.
 */
static Block *_getNextBlockBySize(const Block *current)
{
    if (current == NULL) return NULL;
    uint8_t *next_addr = ((uint8_t*)current) + current->size;
    // Check if we're still within heap bounds
    if (next_addr >= _heapStart + _heapSize) return NULL;
    return (Block*)next_addr;
}

/*
 * Dumps the allocator. You should not need to modify this.
 */
void dumpAllocator()
{
	// See lab tutorial
}

/*
 * Round the integer up to the block header size (16 Bytes).
 */
uint64_t roundUp(uint64_t n)
{
    return (n + HEADER_SIZE - 1) & INV_HEADER_SIZE_MASK;
}

/* Helper function that allocates a block 
 * takes as first argument the address of the next pointer that needs to be updated (!)
 */
static void *allocate_block(Block **update_next, Block *block, uint64_t new_size)
{
    // Remove block from free list
    *update_next = block->next;
    
    // If block is larger than needed, split it
    if (block->size > new_size + HEADER_SIZE) {
        Block *new_free = (Block*)((uint8_t*)block + new_size);
        new_free->size = block->size - new_size;
        new_free->next = *update_next;
        *update_next = new_free;
        block->size = new_size;
    }
    
    // Mark block as allocated
    block->next = ALLOCATED_BLOCK_MAGIC;
    return block->data;
}

void *my_malloc(uint64_t size)
{
    if (size == 0) return NULL;
    
    // Round up size to include header and alignment
    uint64_t total_size = roundUp(size + HEADER_SIZE);
    
    // Check if the requested size is too large for even an extended heap
    if (total_size > HEAP_SIZE - HEADER_SIZE) {
        return NULL;
    }
    
    // Find best fit block
    Block **update_ptr = &_firstFreeBlock;
    Block *best_block = NULL;
    Block **best_update = NULL;
    uint64_t best_size = UINT64_MAX;
    
    while (*update_ptr != NULL) {
        if ((*update_ptr)->size >= total_size && (*update_ptr)->size < best_size) {
            best_block = *update_ptr;
            best_update = update_ptr;
            best_size = (*update_ptr)->size;
        }
        update_ptr = &(*update_ptr)->next;
    }
    
    // If no suitable block found, try to extend heap
    if (best_block == NULL) {
        // Calculate new heap size
        uint64_t new_size = _heapSize + HEAP_SIZE;
        
        // Try to extend the heap
        uint8_t *new_heap = allocHeap(_heapStart, new_size);
        if (new_heap == NULL) {
            return NULL;
        }
        
        // Create new free block at the end of current heap
        Block *new_block = (Block*)(_heapStart + _heapSize);
        new_block->size = HEAP_SIZE;
        
        // Insert the new block into the free list
        new_block->next = _firstFreeBlock;
        _firstFreeBlock = new_block;
        
        // Update heap size
        _heapSize = new_size;
        
        // Try allocation again with the new block
        return my_malloc(size);
    }
    
    return allocate_block(best_update, best_block, total_size);
}


/* Helper function to merge two freelist blocks.
 * Assume: block1 is at a lower address than block2
 * Does nothing if blocks are not neighbors (i.e. if block1 address + block1 size is not block2 address)
 * Otherwise, merges block by merging block2 into block1 (updates block1's size and next pointer
 */
static void merge_blocks(Block *block1, Block *block2)
{
	(void)block1;
	(void)block2;
	/* TODO: Implement */
	/* Note: Again this is not mandatory but possibly useful to put this in a separate
	 * function called by my_free */
}


void my_free(void *address)
{
    if (address == NULL) return;
    
    // Get block header from data pointer
    Block *block = (Block*)((uint8_t*)address - HEADER_SIZE);
    
    // Verify this is an allocated block
    if (block->next != ALLOCATED_BLOCK_MAGIC) return;
    
    // Find where to insert in free list (keeping address order)
    Block **insert_ptr = &_firstFreeBlock;
    while (*insert_ptr != NULL && *insert_ptr < block) {
        insert_ptr = &(*insert_ptr)->next;
    }
    
    // Insert block into free list
    block->next = *insert_ptr;
    *insert_ptr = block;
    
    // Try to merge with next block
    Block *next_block = _getNextBlockBySize(block);
    if (next_block && next_block->next != ALLOCATED_BLOCK_MAGIC) {
        Block **next_ptr = &block->next;
        while (*next_ptr != next_block) {
            next_ptr = &(*next_ptr)->next;
        }
        block->size += next_block->size;
        *next_ptr = next_block->next;
    }
    
    // Try to merge with previous block
    Block *prev_block = _firstFreeBlock;
    Block **prev_ptr = &_firstFreeBlock;
    while (prev_block != block) {
        Block *next = _getNextBlockBySize(prev_block);
        if (next == block && prev_block->next != ALLOCATED_BLOCK_MAGIC) {
            prev_block->size += block->size;
            prev_block->next = block->next;
            return;
        }
        prev_ptr = &prev_block->next;
        prev_block = *prev_ptr;
    }
}


