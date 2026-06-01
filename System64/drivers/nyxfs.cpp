#include "nyxfs.hpp"
#include "../../NyxApps/write.hpp"
#include "disk.hpp" // Sibling include
#include <stddef.h>

extern "C" void* kmalloc(size_t size);
extern "C" void kfree(void* ptr);
bool memcmp_simple(const char* a, const char* b, int len) {
    for (int i = 0; i < len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

namespace NyxFS {

    static void strcpy_simple(char* dest, const char* src, int max_len) {
        for (int i = 0; i < max_len; i++) {
            dest[i] = src[i];
            if (src[i] == '\0') break;
        }
    }

    bool format() {
        uint8_t buffer[512] __attribute__((aligned(16)));
        Superblock* sb = (Superblock*)buffer;
        
        for (int i = 0; i < 512; i++) buffer[i] = 0;

        const char* custom_magic = "NyxFS. Never gonna give you up, never gonna let you down! why did the chicken cross the road?";
        strcpy_simple(sb->magic, custom_magic, 93);
        
        sb->total_files = 0;
        sb->first_node_sector = NODE_START_LBA;
        sb->first_data_sector = DATA_START_LBA;
        sb->total_sectors = 10000; 

        if (!Disk::write_sector(SUPERBLOCK_LBA, buffer)) return false;

        for (int i = 0; i < 512; i++) buffer[i] = 0;
        for (uint32_t sector = NODE_START_LBA; sector < NODE_START_LBA + NODE_SECTORS; sector++) {
            if (!Disk::write_sector(sector, buffer)) return false;
        }

        return true;
    }

    bool mount() {
        uint8_t buffer[512] __attribute__((aligned(16)));
        if (!Disk::read_sector(SUPERBLOCK_LBA, buffer)) return false;

        Superblock* sb = (Superblock*)buffer;
        const char* expected_magic = "NyxFS. Never gonna give you up, never gonna let you down! why did the chicken cross the road?";
        
        // If the strings DO NOT match, format the fresh disk image
        if (memcmp_simple(sb->magic, expected_magic, 93) == false) {
            print("[ DEBUG ] Magic mismatch! Formatting disk...\n");
            //asm volatile("hlt");
            if (!format()) return false;
            return true; 
        }

        // Safely find the absolute end of your existing RAM list instead of erasing it!
        Node* tail = all_nodes_head;
        if (tail != nullptr) {
            while (tail->next != nullptr) {
                tail = tail->next;
            }
        }

        uint8_t sector_buf[512] __attribute__((aligned(16)));
        for (uint32_t s = 0; s < NODE_SECTORS; s++) {
            if (!Disk::read_sector(NODE_START_LBA + s, sector_buf)) return false;
            
            FileEntry* entries = (FileEntry*)sector_buf;
            for (int e = 0; e < 10; e++) { 
                if (entries[e].flags == 1 || entries[e].flags == 2) { // Match file or folder
                    
                    Node* new_node = new Node();
                    strcpy_simple(new_node->name, entries[e].name, 32);
                    new_node->size = entries[e].size;
                    new_node->used = true;
                    new_node->is_folder = (entries[e].flags == 2);
                    new_node->next = nullptr;
                    // Set parent to current_dir_ptr (which is now 'root' because fs_init ran first!)
                    new_node->parent = current_dir_ptr;

                    // Read 1KB payload content chunks
                    uint8_t data_buf[512] __attribute__((aligned(16)));
                    if (!Disk::read_sector(entries[e].start_lba, data_buf)) return false;
                    for (int b = 0; b < 512; b++) new_node->content[b] = data_buf[b];
                    
                    if (!Disk::read_sector(entries[e].start_lba + 1, data_buf)) return false;
                    for (int b = 0; b < 512; b++) new_node->content[512 + b] = data_buf[b];

                    // Append cleanly onto your running tail pointer
                    if (all_nodes_head == nullptr) {
                        all_nodes_head = new_node;
                        tail = new_node;
                    } else {
                        tail->next = new_node;
                        tail = new_node;
                    }
                }
            }
        }
        return true;
    }

    bool sync() {
        uint8_t sector_buf[512] __attribute__((aligned(16)));
        uint32_t current_node_sector = NODE_START_LBA;
        int entry_index = 0;
        uint32_t current_data_lba = DATA_START_LBA;

        for (int i = 0; i < 512; i++) sector_buf[i] = 0;

        Node* current_ram_node = all_nodes_head;
        uint32_t active_file_count = 0;

        while (current_ram_node != nullptr) {
            // Recalculate entries pointer relative to the current sector_buf base address
            FileEntry* entries = (FileEntry*)sector_buf;
            
            strcpy_simple(entries[entry_index].name, current_ram_node->name, 32);
            entries[entry_index].size = current_ram_node->size;
            entries[entry_index].start_lba = current_data_lba;
            entries[entry_index].flags = current_ram_node->is_folder ? 2 : 1; 

            uint8_t data_buf[512] __attribute__((aligned(16)));
            
            // Flush First 512 Bytes
            for (int b = 0; b < 512; b++) data_buf[b] = current_ram_node->content[b];
            if (!Disk::write_sector(current_data_lba, data_buf)) {
                print("\n[ CRITICAL ] Disk Write FAILED - Data LBA Block 1\n");
                return false;
            }
            
            // Flush Second 512 Bytes
            for (int b = 0; b < 512; b++) data_buf[b] = current_ram_node->content[512 + b];
            if (!Disk::write_sector(current_data_lba + 1, data_buf)) {
                print("\n[ CRITICAL ] Disk Write FAILED - Data LBA Block 2\n");
                return false;
            }

            current_data_lba += 2; 
            active_file_count++;
            entry_index++;

            if (entry_index == 10) {
                if (!Disk::write_sector(current_node_sector, sector_buf)) {
                    print("\n[ CRITICAL ] Disk Write FAILED - Node Table Sector\n");
                    return false;
                }
                current_node_sector++;
                entry_index = 0;
                for (int i = 0; i < 512; i++) sector_buf[i] = 0; 
            }

            current_ram_node = current_ram_node->next;
        }

        if (entry_index > 0) {
            if (!Disk::write_sector(current_node_sector, sector_buf)) {
                print("\n[ CRITICAL ] Disk Write FAILED - Trailing Node Table Sector\n");
                return false;
            }
            current_node_sector++;
        }

        // Flush updated global metrics to Superblock
        for (int i = 0; i < 512; i++) sector_buf[i] = 0;
        Superblock* sb = (Superblock*)sector_buf;
        const char* custom_magic = "NyxFS. Never gonna give you up, never gonna let you down! why did the chicken cross the road?";
        strcpy_simple(sb->magic, custom_magic, 93);
        sb->total_files = active_file_count;
        sb->first_node_sector = NODE_START_LBA;
        sb->first_data_sector = DATA_START_LBA;
        sb->total_sectors = 10000;

        if (!Disk::write_sector(SUPERBLOCK_LBA, sector_buf)) {
            print("\n[ CRITICAL ] Disk Write FAILED - Superblock Synchronization\n");
            return false;
        }

        return true;
    }
}