#pragma once

#include <stdint.h>

struct Node {
    char name[32];
    bool used;        // We keep this for safety, though 'new' handles existence
    bool is_folder;
    
    Node* parent;     // Changed from 'int' to 'Node*'
    Node* next;       // Link for the global list
    
    char content[1024];
    int size;
};

// Externs for the globals used in vmkrnl.cpp
extern Node* all_nodes_head;
extern Node* current_dir_ptr;

/* FS functions */
Node* fs_find(const char* name);
Node* fs_create(const char* name, bool folder);
void fs_delete(const char* name);

/* String helper */
bool strcmp_simple(const char* a, const char* b);