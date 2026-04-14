#pragma once
#include <stdint.h>
#include <stddef.h>

#define BLOCK_SIZE 4096
#define BLOCKS_PER_BYTE 8

void pmm_init(size_t mem_size, uint64_t bitmap_start);
void pmm_mark_used(uint64_t addr);
void pmm_mark_free(uint64_t addr);
void* pmm_alloc_block();
void pmm_free_block(void* ptr);