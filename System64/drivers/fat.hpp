#ifndef FAT_HPP
#define FAT_HPP

#include "disk.hpp"

// Ensure structures are packed to match raw disk layout bytes perfectly
#pragma pack(push, 1)
struct FAT_BPB {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_short;
    uint8_t  media;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    // Extended Boot Record (EBR)
    uint8_t  drive_num;
    uint8_t  reserved;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     label[11];
    char     fs_type[8];
};

struct FAT_Dir_Entry {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_ms;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t cluster_high;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_low;
    uint32_t size;
};
#pragma pack(pop)

// External kernel printing utilities from vmkrnl
extern "C" void print(const char* str);
extern "C" void putchar(char c);
extern "C" void print_int(int val);

namespace FAT {
    bool mount_drive();
    void list_root_directory(); // Legacy compatibility function
    void list_directory(uint16_t current_dir_cluster); // Modern multi-directory engine
    bool format_drive();
    
    // Directory manipulation and navigation engines
    bool traverse_directory(const char* target_dir, uint16_t& current_cluster);
    bool create_file(const char* name, bool is_folder, uint16_t current_dir_cluster);
    bool delete_entry(const char* name, uint16_t current_cluster);
    void clear_fat_chain(uint16_t starting_cluster);
    bool write_file_data(const char* name, uint16_t current_dir_cluster, const uint8_t* data_buffer, uint32_t data_size);
    int read_file_data(const char* name, uint16_t current_dir_cluster, uint8_t* output_buffer);
    void list_directory_to_cache(uint16_t current_dir_cluster, void (*callback)(const char*, bool, uint16_t, uint32_t));
}
#endif