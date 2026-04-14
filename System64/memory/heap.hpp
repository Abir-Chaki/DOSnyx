#pragma once
#include <stddef.h>

void heap_init();
extern "C" void* kmalloc(size_t size);
extern "C" void kfree(void* ptr);