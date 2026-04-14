#include "heap.hpp"
#include <stdint.h>

struct HeapSegmentHeader {
    size_t size;
    HeapSegmentHeader* next;
    HeapSegmentHeader* last;
    bool free;
};

static HeapSegmentHeader* first_header;

void heap_init() {
    // We start the heap at 2MB (0x200000)
    first_header = (HeapSegmentHeader*)0x200000;
    // For now, let's give it that same 2MB of total space
    first_header->size = 0x200000 - sizeof(HeapSegmentHeader); 
    first_header->next = nullptr;
    first_header->last = nullptr;
    first_header->free = true;
}

extern "C" void* kmalloc(size_t size) {
    // 16-byte alignment
    size = (size + 15) & ~15;

    HeapSegmentHeader* current = first_header;
    while (current != nullptr) {
        if (current->free && current->size >= size) {
            // Split the block if it's significantly larger than requested
            if (current->size > size + sizeof(HeapSegmentHeader) + 16) {
                HeapSegmentHeader* new_header = (HeapSegmentHeader*)((uint8_t*)current + sizeof(HeapSegmentHeader) + size);
                
                new_header->free = true;
                new_header->size = current->size - size - sizeof(HeapSegmentHeader);
                new_header->next = current->next;
                new_header->last = current;

                if (current->next != nullptr) current->next->last = new_header;
                current->next = new_header;
                current->size = size;
            }
            
            current->free = false;
            return (void*)((uint8_t*)current + sizeof(HeapSegmentHeader));
        }
        current = current->next;
    }
    return nullptr; // Truly out of memory
}

extern "C" void kfree(void* ptr) {
    if (ptr == nullptr) return;

    HeapSegmentHeader* header = (HeapSegmentHeader*)((uint8_t*)ptr - sizeof(HeapSegmentHeader));
    header->free = true;

    // Merge with next if free (Coalescing)
    if (header->next != nullptr && header->next->free) {
        header->size += header->next->size + sizeof(HeapSegmentHeader);
        header->next = header->next->next;
        if (header->next != nullptr) header->next->last = header;
    }

    // Merge with previous if free
    if (header->last != nullptr && header->last->free) {
        header->last->size += header->size + sizeof(HeapSegmentHeader);
        header->last->next = header->next;
        if (header->next != nullptr) header->next->last = header->last;
    }
}