#include "heap.hpp"
#include <stdint.h>

static uint8_t* heap_start = (uint8_t*)0x200000; // 2MB
static uint8_t* heap_current = heap_start;
static uint8_t* heap_end = (uint8_t*)0x400000;   // 4MB limit

void heap_init()
{
    heap_current = heap_start;
}

void* kmalloc(size_t size)
{
    if (heap_current + size >= heap_end)
        return nullptr;

    void* addr = heap_current;
    heap_current += size;

    // align to 16 bytes
    heap_current = (uint8_t*)(((uint64_t)heap_current + 15) & ~15);

    return addr;
}

void kfree(void* ptr)
{
    // not implemented yet (bump allocator can't free)
}