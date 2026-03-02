#pragma once
#include <stddef.h>

void heap_init();
void* kmalloc(size_t size);
void kfree(void* ptr);