#ifndef WRITE_HPP
#define WRITE_HPP

#include <stdint.h>

extern "C" void clear_workspace();
extern "C" void print(const char* str);
extern "C" void putchar(char c);

// Add global VFS drive tracking states
extern int current_drive_id;
extern uint16_t fat_current_cluster;

// Unified entry point for your shell or main desktop panel interface
void write_app_start(const char* filename);

#endif