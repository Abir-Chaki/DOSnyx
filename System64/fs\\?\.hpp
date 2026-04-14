#pragma once

#include <stdint.h>

#define MAX_NODES 64

struct Node {
    char name[16];
    bool used;
    bool is_folder;
    int parent;

    char content[1024];
    int size;
};

extern Node nodes[MAX_NODES];
extern int current_dir;

/* FS functions */
int fs_find(const char* name);
int fs_create(const char* name, bool folder);
void fs_delete(const char* name);

/* String helper */
bool strcmp_simple(const char* a, const char* b);