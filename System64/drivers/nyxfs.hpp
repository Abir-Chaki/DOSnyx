#pragma once
#include <stdint.h>
#include "../fs.hpp" // Steps up out of drivers/ into System64/

namespace NyxFS {

    struct Superblock {
        char magic[93];             // "NyxFS. Never gonna give you up..."
        uint32_t total_files;       
        uint32_t first_node_sector; 
        uint32_t first_data_sector; 
        uint32_t total_sectors;     
        uint8_t padding[407];       
    } __attribute__((packed));

    struct FileEntry {
        char name[32];              
        uint32_t size;              
        uint32_t start_lba;         
        uint8_t flags;              
        uint8_t padding[7];        
    } __attribute__((packed));      

    constexpr uint32_t SUPERBLOCK_LBA = 1;
    constexpr uint32_t NODE_START_LBA = 2;
    constexpr uint32_t NODE_SECTORS   = 10; 
    constexpr uint32_t DATA_START_LBA = 12; 

    bool format();                  
    bool mount();                   
    bool sync();                    
}

bool strcmp_simple(const char* a, const char* b);
