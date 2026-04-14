#include <stddef.h>

// Link to your existing heap functions
extern "C" void* kmalloc(size_t size);
extern "C" void kfree(void* ptr);

/* --- Standard C++ Operators --- */

void* operator new(size_t size) {
    return kmalloc(size);
}

void* operator new[](size_t size) {
    return kmalloc(size);
}

void operator delete(void* ptr) noexcept {
    kfree(ptr);
}

void operator delete[](void* ptr) noexcept {
    kfree(ptr);
}

// C++20 sized-delete compatibility
void operator delete(void* ptr, size_t size) noexcept {
    (void)size;
    kfree(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
    (void)size;
    kfree(ptr);
}

/* --- Placement New --- */
// Essential for calling constructors at specific memory locations
void* operator new(size_t size, void* ptr) noexcept {
    (void)size;
    return ptr;
}

void* operator new[](size_t size, void* ptr) noexcept {
    (void)size;
    return ptr;
}