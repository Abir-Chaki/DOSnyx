#include "pmm.hpp"

static uint8_t* bitmap;
static size_t total_blocks;

void pmm_init(size_t mem_size, uint64_t bitmap_start) {
    total_blocks = mem_size / BLOCK_SIZE;
    bitmap = (uint8_t*)bitmap_start;

    // Initially mark everything as "used" (1), then free what we know is safe
    for (size_t i = 0; i < total_blocks / BLOCKS_PER_BYTE; i++) {
        bitmap[i] = 0xFF; 
    }
}
void pmm_free_block(void* ptr) {
    // This just calls our existing mark_free logic
    pmm_mark_free((uint64_t)ptr);
}

void pmm_mark_free(uint64_t addr) {
    uint64_t block = addr / BLOCK_SIZE;
    bitmap[block / 8] &= ~(1 << (block % 8));
}
void pmm_mark_used(uint64_t addr) {
    uint64_t block = addr / BLOCK_SIZE;
    bitmap[block / 8] |= (1 << (block % 8));
}

void* pmm_alloc_block() {
    for (size_t i = 0; i < total_blocks / 8; i++) {
        if (bitmap[i] != 0xFF) { // There is at least one free bit here
            for (int j = 0; j < 8; j++) {
                if (!(bitmap[i] & (1 << j))) {
                    uint64_t addr = (i * 8 + j) * BLOCK_SIZE;
                    pmm_mark_used(addr);
                    return (void*)addr;
                }
            }
        }
    }
    return nullptr; // Out of memory!
}