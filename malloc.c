#define USE_REAL_SBRK 1
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
 * takes as first argument the address of the next pointer that needs to be updated
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

// Add at the top with other globals
static AllocType _currentStrategy = ALLOC_BESTFIT;
static Block *_lastAllocatedBlock = NULL;  // For next-fit strategy

void setAllocationStrategy(AllocType type) {
    _currentStrategy = type;
    _lastAllocatedBlock = NULL;  // Reset last allocation point
}

/*
 * Helper function that finds a suitable block based on current allocation strategy.
 * Returns the found block and sets prev_out to its previous block.
 */
static Block* find_block(uint64_t needed_size, Block **prev_out) {
    Block *prev = NULL;
    Block *current = _firstFreeBlock;
    Block *best_block = NULL;
    Block *best_prev = NULL;
    uint64_t best_size = UINT64_MAX;
    uint64_t worst_size = 0;
    
    if (_currentStrategy == ALLOC_NEXTFIT && _lastAllocatedBlock != NULL) {
        current = _lastAllocatedBlock->next;
        Block *temp = _firstFreeBlock;
        while (temp != NULL && temp != current) {
            prev = temp;
            temp = temp->next;
        }
    }
    
    Block *start_block = current;  // Remember where we started for next-fit
    bool wrapped = false;          // For next-fit to avoid infinite loop
    
    do {
        if (current == NULL) {
            if (_currentStrategy == ALLOC_NEXTFIT && !wrapped) {
                current = _firstFreeBlock;
                prev = NULL;
                wrapped = true;
                continue;
            }
            break;
        }
        
        if (current->size >= needed_size) {
            switch (_currentStrategy) {
                case ALLOC_FIRSTFIT:
                    *prev_out = prev;
                    return current;
                
                case ALLOC_NEXTFIT:
                    *prev_out = prev;
                    _lastAllocatedBlock = current;
                    return current;
                
                case ALLOC_BESTFIT:
                    if (current->size < best_size) {
                        best_block = current;
                        best_prev = prev;
                        best_size = current->size;
                    }
                    break;
                
                case ALLOC_WORSTFIT:
                    if (current->size > worst_size) {
                        best_block = current;
                        best_prev = prev;
                        worst_size = current->size;
                    }
                    break;
            }
        }
        
        prev = current;
        current = current->next;
        
    } while (_currentStrategy == ALLOC_NEXTFIT && 
             (current != start_block || !wrapped));
    
    *prev_out = best_prev;
    return best_block;
}

void *my_malloc(uint64_t size)
{
    if (size == 0) return NULL;
    
    uint64_t needed_size = roundUp(size + HEADER_SIZE);
    if (needed_size > HEAP_SIZE - HEADER_SIZE) {
        return NULL;
    }
    
    Block *prev = NULL;
    Block *best_block = find_block(needed_size, &prev);
    
    if (best_block != NULL) {
        // Remove from free list
        if (prev == NULL) {
            _firstFreeBlock = best_block->next;
        } else {
            prev->next = best_block->next;
        }
        
        // Split if block is too big
        if (best_block->size > needed_size + HEADER_SIZE) {
            Block *new_block = (Block*)((uint8_t*)best_block + needed_size);
            new_block->size = best_block->size - needed_size;
            new_block->next = _firstFreeBlock;
            _firstFreeBlock = new_block;
            best_block->size = needed_size;
        }
        
        best_block->next = ALLOCATED_BLOCK_MAGIC;
        return best_block->data;
    }
    
    // Try to extend heap
    uint64_t new_size = _heapSize + HEAP_SIZE;
    if (allocHeap(_heapStart, new_size) != NULL) {
        Block *new_block = (Block*)(_heapStart + _heapSize);
        new_block->size = HEAP_SIZE;
        new_block->next = _firstFreeBlock;
        _firstFreeBlock = new_block;
        _heapSize = new_size;
        return my_malloc(size);
    }
    
    return NULL;
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
        if (_lastAllocatedBlock == next_block) {
            _lastAllocatedBlock = block;
        }
        
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
            if (_lastAllocatedBlock == block) {
                _lastAllocatedBlock = prev_block;
            }
            
            prev_block->size += block->size;
            prev_block->next = block->next;
            return;
        }
        prev_ptr = &prev_block->next;
        prev_block = *prev_ptr;
    }
}

MallocStat getAllocStatistics() {
    MallocStat stats = {0, 0, 0};  // Initialize all fields to 0
    uint64_t total_free = 0;
    
    // Traverse free list to gather statistics
    Block *current = _firstFreeBlock;
    while (current != NULL) {
        stats.nFree++;
        
        // Add to total free space
        total_free += current->size;
        
        // Update largest free block if needed
        if (current->size > stats.largestFree) {
            stats.largestFree = current->size;
        }
        
        current = current->next;
    }
    
    // Calculate average (if there are free blocks)
    if (stats.nFree > 0) {
        stats.avgFree = total_free / stats.nFree;
    }
    
    return stats;
}


