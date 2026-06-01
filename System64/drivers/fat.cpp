#include "fat.hpp"

namespace FAT {
    static FAT_BPB active_bpb;
    static uint32_t root_dir_sector = 0;
    static uint32_t data_sector = 0;
    static bool is_fat16 = false;
    static uint16_t find_free_cluster();

    // Fast memory copy utility helper
    static void fat_memcpy(void* dest, const void* src, int n) {
        char* d = (char*)dest;
        const char* s = (const char*)src;
        for (int i = 0; i < n; i++) d[i] = s[i];
    }

    // Kernel string comparison utility helper
    static bool strcmp_simple(const char* str1, const char* str2) {
        int i = 0;
        while (str1[i] != '\0' || str2[i] != '\0') {
            if (str1[i] != str2[i]) return false;
            i++;
        }
        return true;
    }

    // Helper to sanitize standard FAT 8.3 space padding into clean text
    static void parse_fat_name(const FAT_Dir_Entry* entry, char* out_name) {
        int p = 0;
        for (int i = 0; i < 8; i++) {
            if (entry->name[i] != ' ' && entry->name[i] != '\0') {
                out_name[p++] = entry->name[i];
            }
        }
        // If it's not a volume label and has an extension, append it
        if (!(entry->attr & 0x08) && entry->ext[0] != ' ' && entry->ext[0] != '\0') {
            out_name[p++] = '.';
            for (int i = 0; i < 3; i++) {
                if (entry->ext[i] != ' ' && entry->ext[i] != '\0') {
                    out_name[p++] = entry->ext[i];
                }
            }
        }
        out_name[p] = '\0';
    }

    // Helper function to format input strings to classic 8.3 structure padded with spaces
    static void format_raw_name_to_fat(const char* raw_name, uint8_t* fat_name) {
        // Initialize with whitespace characters
        for (int i = 0; i < 11; i++) fat_name[i] = ' ';

        int i = 0;
        // Copy filename part up to the dot divider or maximum 8 char string bounds
        while (raw_name[i] != '\0' && raw_name[i] != '.' && i < 8) {
            char c = raw_name[i];
            if (c >= 'a' && c <= 'z') c -= 32; // Enforce pure uppercase compliance
            fat_name[i] = c;
            i++;
        }

        // Fast-forward cursor to character dot position if name execution was short
        while (raw_name[i] != '\0' && raw_name[i] != '.') i++;

        if (raw_name[i] == '.') {
            i++; // Move beyond the dot token separator
            int ext_idx = 8;
            while (raw_name[i] != '\0' && ext_idx < 11) {
                char c = raw_name[i];
                if (c >= 'a' && c <= 'z') c -= 32;
                fat_name[ext_idx++] = c;
                i++;
            }
        }
    }

    // Mounts a physical target storage drive sector sequence
    bool mount_drive() {
        uint8_t sector_buf[512];
        
        // Read raw Boot Sector 0 from disk controller
        if (!Disk::read_sector(0, sector_buf)) {
            return false;
        }

        FAT_BPB* bpb = (FAT_BPB*)sector_buf;
        
        // Safety verification: verify the mandatory trailing MBR signature bytes
        if (sector_buf[510] != 0x55 || sector_buf[511] != 0xAA) {
            return false;
        }

        // Cache the parsed BIOS Parameter Block parameters into kernel memory
        fat_memcpy(&active_bpb, bpb, sizeof(FAT_BPB));

        // Calculate critical filesystem structural start offsets
        root_dir_sector = active_bpb.reserved_sectors + (active_bpb.fat_count * active_bpb.sectors_per_fat);
        uint32_t root_dir_sectors_count = ((active_bpb.root_entries * 32) + 511) / 512;
        data_sector = root_dir_sector + root_dir_sectors_count;

        // Auto-detect bitwidth allocation via data cluster capacity logic
        uint32_t total_sectors = (active_bpb.total_sectors_short != 0) ? active_bpb.total_sectors_short : active_bpb.total_sectors_large;
        uint32_t data_sectors_total = total_sectors - data_sector;
        uint32_t total_clusters = data_sectors_total / active_bpb.sectors_per_cluster;

        is_fat16 = (total_clusters >= 4085);
        return true;
    }

    // Low-level block formatter to establish a standard FAT16 footprint on a raw drive
    bool format_drive() {
        uint8_t sector_buf[512];
        
        // Zero out the buffer before writing structural details
        for (int i = 0; i < 512; i++) sector_buf[i] = 0;

        // Compile a standard, valid mini-FAT16 structure footprint
        FAT_BPB* bpb = (FAT_BPB*)sector_buf;
        bpb->jmp[0] = 0xEB; bpb->jmp[1] = 0x3C; bpb->jmp[2] = 0x90; // Standard jump opcode
        fat_memcpy(bpb->oem, "DOSNYX30", 8);
        bpb->bytes_per_sector = 512;
        bpb->sectors_per_cluster = 4;   // 2KB clusters
        bpb->reserved_sectors = 4;      // Leave space for boot record safely
        bpb->fat_count = 2;             // Main FAT + Backup FAT
        bpb->root_entries = 512;        // Standard root capacity
        bpb->total_sectors_short = 0;   // Use large sector count field
        bpb->media = 0xF8;              // Fixed Hard Disk descriptor
        bpb->sectors_per_fat = 256;     // Covers our cluster mapping array tables
        bpb->total_sectors_large = 204800; // ~100MB volume track
        bpb->drive_num = 0x80;
        bpb->boot_sig = 0x29;
        bpb->volume_id = 0xDEADC0DE;
        fat_memcpy(bpb->label, "DOSNYX DISK", 11);
        fat_memcpy(bpb->fs_type, "FAT16   ", 8);

        // Add mandatory hardware boot validation signatures to trailing sector bytes
        sector_buf[510] = 0x55;
        sector_buf[511] = 0xAA;

        print("Writing logical volume Master Boot Sector... ");
        if (!Disk::write_sector(0, sector_buf)) return false;
        print("Done.\n");

        // Calculate location of tracking segments to wipe them clear
        uint32_t fat_start = bpb->reserved_sectors;
        uint32_t fat_total_sectors = bpb->fat_count * bpb->sectors_per_fat;
        uint32_t root_start = fat_start + fat_total_sectors;
        uint32_t root_total_sectors = (bpb->root_entries * 32) / 512;

        // Clear out the tracking table buffers to initialize allocation structures
        for (int i = 0; i < 512; i++) sector_buf[i] = 0;
        
        // Initialize the tracking allocation table entries header
        sector_buf[0] = 0xF8; // Media Descriptor Match
        sector_buf[1] = 0xFF;
        sector_buf[2] = 0xFF;
        sector_buf[3] = 0xFF; // FAT16 cluster allocation end-of-chain signature caps

        print("Clearing allocation structures... ");
        if (!Disk::write_sector(fat_start, sector_buf)) return false;
        
        // Wipe remaining allocated tracking buffers clear
        for (int i = 0; i < 512; i++) sector_buf[i] = 0;
        for (uint32_t s = fat_start + 1; s < fat_start + fat_total_sectors; s++) {
            if (!Disk::write_sector(s, sector_buf)) return false;
        }
        print("Done.\n");

        print("Zero-mapping clean workspace Root Directory blocks... ");
        for (uint32_t s = root_start; s < root_start + root_total_sectors; s++) {
            if (!Disk::write_sector(s, sector_buf)) return false;
        }
        print("Done.\n");

        return true;
    }

    // Reads raw sector blocks to spit out active directory entries anywhere on disk
    void list_directory(uint16_t current_dir_cluster) {
        uint8_t sector_buf[512];
        uint32_t target_sector = 0;
        uint32_t sectors_to_read = 0;
        bool files_found = false;

        // Dynamic targeting based on execution depth mapping (Root directory vs subfolder)
        if (current_dir_cluster == 0) {
            target_sector = root_dir_sector;
            sectors_to_read = (active_bpb.root_entries * 32) / 512;
        } else {
            target_sector = data_sector + ((current_dir_cluster - 2) * active_bpb.sectors_per_cluster);
            sectors_to_read = active_bpb.sectors_per_cluster;
        }

        for (uint32_t s = 0; s < sectors_to_read; s++) {
            if (!Disk::read_sector(target_sector + s, sector_buf)) {
                print("Error: Disk IO failure reading entry bounds.\n");
                return;
            }

            FAT_Dir_Entry* entries = (FAT_Dir_Entry*)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (entries[e].name[0] == 0x00) {
                    if (!files_found) print(" (Directory is empty)\n");
                    return; 
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) continue; 
                if (entries[e].attr == 0x0F) continue;            

                char clean_name[13];
                parse_fat_name(&entries[e], clean_name);

                // =============================================================
                // CRITICAL TERMINAL FILTER: Hide dot entries from raw CLI listing
                // =============================================================
                if (clean_name[0] == '.') {
                    continue; 
                }

                files_found = true;
                if (entries[e].attr & 0x10) {
                    print("[DIR]  "); print(clean_name); print("\n");
                } else if (entries[e].attr & 0x08) {
                    print("[LBL]  Volume Label: "); print(clean_name); print("\n");
                } else {
                    print("[FILE] "); print(clean_name); print("   ");
                    print_int(entries[e].size); print(" Bytes\n");
                }
            }
        }
    }

    // Keep legacy support redirecting to Root cluster mapping
    void list_root_directory() {
        list_directory(0);
    }

    // Core directory routing logic parsing engine
    bool traverse_directory(const char* target_dir, uint16_t& current_cluster) {
        uint8_t sector_buf[512];
        uint32_t target_sector = 0;
        uint32_t sectors_to_read = 0;

        if (current_cluster == 0) {
            target_sector = root_dir_sector;
            sectors_to_read = (active_bpb.root_entries * 32) / 512;
        } else {
            target_sector = data_sector + ((current_cluster - 2) * active_bpb.sectors_per_cluster);
            sectors_to_read = active_bpb.sectors_per_cluster;
        }

        for (uint32_t s = 0; s < sectors_to_read; s++) {
            if (!Disk::read_sector(target_sector + s, sector_buf)) return false;

            FAT_Dir_Entry* entries = (FAT_Dir_Entry*)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (entries[e].name[0] == 0x00) return false;
                if ((uint8_t)entries[e].name[0] == 0xE5) continue;
                if (entries[e].attr == 0x0F) continue;

                // Make sure we target matching structural subdirectories
                if (entries[e].attr & 0x10) {
                    char clean_name[13];
                    parse_fat_name(&entries[e], clean_name);

                    if (strcmp_simple(clean_name, target_dir)) {
                        // Extract cluster link allocation maps
                        current_cluster = entries[e].cluster_low;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Primary entry instantiation creation routine
    bool create_file(const char* name, bool is_folder, uint16_t current_dir_cluster) {
        uint8_t sector_buf[512];
        uint32_t target_sector = 0;
        uint32_t sectors_to_read = 0;

        if (current_dir_cluster == 0) {
            target_sector = root_dir_sector;
            sectors_to_read = (active_bpb.root_entries * 32) / 512;
        } else {
            target_sector = data_sector + ((current_dir_cluster - 2) * active_bpb.sectors_per_cluster);
            sectors_to_read = active_bpb.sectors_per_cluster;
        }

        uint8_t formatted_name[11];
        format_raw_name_to_fat(name, formatted_name);

        // Allocate a dedicated data track if this object is a new subdirectory block
        uint16_t new_dir_cluster = 0;
        if (is_folder) {
            new_dir_cluster = find_free_cluster();
            if (new_dir_cluster == 0) return false; // Disk Full

            // Mark the cluster as allocated (End of Chain) inside the primary FAT table
            uint32_t byte_offset = new_dir_cluster * 2;
            uint32_t sector_offset = active_bpb.reserved_sectors + (byte_offset / 512);
            uint32_t byte_inside = byte_offset % 512;

            uint8_t link_buf[512];
            if (!Disk::read_sector(sector_offset, link_buf)) return false;
            *(uint16_t*)&link_buf[byte_inside] = 0xFFFF;
            if (!Disk::write_sector(sector_offset, link_buf)) return false;
            // Mirror write to backup FAT tracker table
            if (!Disk::write_sector(sector_offset + active_bpb.sectors_per_fat, link_buf)) return false;

            // Prepare a clean template sector for the subdirectory data block
            uint8_t dir_sector_template[512];
            for (int i = 0; i < 512; i++) dir_sector_template[i] = 0;

            FAT_Dir_Entry* sub_entries = (FAT_Dir_Entry*)dir_sector_template;

            // Entry 1: Current Directory Link "."
            fat_memcpy(sub_entries[0].name, ".       ", 8);
            fat_memcpy(sub_entries[0].ext, "   ", 3);
            sub_entries[0].attr = 0x10;
            sub_entries[0].cluster_low = new_dir_cluster;

            // Entry 2: Parent Directory Link ".."
            fat_memcpy(sub_entries[1].name, "..      ", 8);
            fat_memcpy(sub_entries[1].ext, "   ", 3);
            sub_entries[1].attr = 0x10;
            sub_entries[1].cluster_low = current_dir_cluster; // Points to parent cluster context

            // Write the structural entries down to the first sector of our allocated cluster
            uint32_t target_dir_sector = data_sector + ((new_dir_cluster - 2) * active_bpb.sectors_per_cluster);
            if (!Disk::write_sector(target_dir_sector, dir_sector_template)) return false;

            // Clear remaining trailing sectors in the new cluster bundle to prevent leftover junk data
            for (int i = 0; i < 512; i++) dir_sector_template[i] = 0;
            for (uint8_t s = 1; s < active_bpb.sectors_per_cluster; s = s + 1) {
                if (!Disk::write_sector(target_dir_sector + s, dir_sector_template)) return false;
            }
        }

        // Search for an available entry position slot in parent directory blocks
        for (uint32_t s = 0; s < sectors_to_read; s++) {
            if (!Disk::read_sector(target_sector + s, sector_buf)) return false;

            FAT_Dir_Entry* entries = (FAT_Dir_Entry*)sector_buf;

            for (int e = 0; e < 16; e++) {
                uint8_t first_byte = (uint8_t)entries[e].name[0];

                if (first_byte == 0x00 || first_byte == 0xE5) {
                    // Populate name matrices
                    for (int i = 0; i < 8; i++) entries[e].name[i] = formatted_name[i];
                    for (int i = 0; i < 3; i++) entries[e].ext[i] = formatted_name[8 + i];

                    entries[e].attr = is_folder ? 0x10 : 0x20;
                    entries[e].nt_res = 0;
                    entries[e].crt_time_ms = 0;
                    entries[e].crt_time = 0;
                    entries[e].crt_date = 0;
                    entries[e].lst_acc_date = 0;
                    entries[e].cluster_high = 0;
                    entries[e].wrt_time = 0;
                    entries[e].wrt_date = 0;
                    entries[e].cluster_low = is_folder ? new_dir_cluster : 0; 
                    entries[e].size = 0;

                    // Flush parent entry block details down to disk sectors
                    return Disk::write_sector(target_sector + s, sector_buf);
                }
            }
        }
        return false;
    }
    void clear_fat_chain(uint16_t starting_cluster) {
        uint16_t current_cluster = starting_cluster;
        uint8_t fat_buffer[512];
        uint32_t active_sector = 0xFFFFFFFF; 

        // Extract starting sector tracker for primary allocation table
        uint32_t fat_start_sector = active_bpb.reserved_sectors;

        // Traverse our FAT16 structure link chain limits
        while (current_cluster >= 2 && current_cluster < 0xFFF8) {
            uint32_t byte_offset = current_cluster * 2; // FAT16 uses 2 bytes per entry
            uint32_t sector_offset = fat_start_sector + (byte_offset / 512);
            uint32_t byte_inside_sector = byte_offset % 512;

            if (sector_offset != active_sector) {
                if (active_sector != 0xFFFFFFFF) {
                    // Flush previously modified tracking sector back to disk
                    Disk::write_sector(active_sector, fat_buffer);
                    
                    // Mirror write to backup FAT tracker for reliability
                    Disk::write_sector(active_sector + active_bpb.sectors_per_fat, fat_buffer);
                }
                
                active_sector = sector_offset;
                if (!Disk::read_sector(active_sector, fat_buffer)) return;
            }

            // Capture link target address before clearing it
            uint16_t next_cluster = *(uint16_t*)&fat_buffer[byte_inside_sector];

            // Set cluster state back to 0x0000 (Available Allocation Space)
            *(uint16_t*)&fat_buffer[byte_inside_sector] = 0x0000;

            current_cluster = next_cluster;
        }

        // Final flush for left over structural dirty cache lines
        if (active_sector != 0xFFFFFFFF) {
            Disk::write_sector(active_sector, fat_buffer);
            Disk::write_sector(active_sector + active_bpb.sectors_per_fat, fat_buffer);
        }
    }
    bool delete_entry(const char* name, uint16_t current_cluster) {
        uint8_t sector_buffer[512];
        uint32_t target_sector = 0;
        uint32_t sectors_to_read = 0;

        // Dynamic targeting using your active BPB mapping specs
        if (current_cluster == 0) {
            target_sector = root_dir_sector;
            sectors_to_read = (active_bpb.root_entries * 32) / 512;
        } else {
            target_sector = data_sector + ((current_cluster - 2) * active_bpb.sectors_per_cluster);
            sectors_to_read = active_bpb.sectors_per_cluster;
        }

        // Format user input into classic 8.3 space-padded bytes
        uint8_t formatted_name[11];
        format_raw_name_to_fat(name, formatted_name);

        for (uint32_t s = 0; s < sectors_to_read; s++) {
            if (!Disk::read_sector(target_sector + s, sector_buffer)) return false;

            FAT_Dir_Entry* entries = (FAT_Dir_Entry*)sector_buffer;

            for (int e = 0; e < 16; e++) {
                uint8_t first_byte = (uint8_t)entries[e].name[0];

                if (first_byte == 0x00) return false; // End of directory list bounds
                if (first_byte == 0xE5) continue;     // Already unlinked slot
                if (entries[e].attr == 0x0F) continue;// Skip Long File Name indicators

                // Match name block using native structural comparison logic
                bool match = true;
                for (int i = 0; i < 8; i++) {
                    if (entries[e].name[i] != formatted_name[i]) { match = false; break; }
                }
                for (int i = 0; i < 3; i++) {
                    if (entries[e].ext[i] != formatted_name[8 + i]) { match = false; break; }
                }

                if (match) {
                    // Pull out starting cluster records before wiping entry
                    uint16_t starting_cluster = entries[e].cluster_low;

                    // STEP A: Overwrite the tracking signature with the unlinked indicator
                    entries[e].name[0] = 0xE5;

                    // Flush modification right back to disk block
                    if (!Disk::write_sector(target_sector + s, sector_buffer)) return false;

                    // STEP B: Unlink data links from File Allocation Table maps
                    if (starting_cluster >= 2) {
                        clear_fat_chain(starting_cluster);
                    }

                    return true;
                }
            }
        }
        return false;
    }
    // Dynamic payload stream allocation engine
    bool write_file_data(const char* name, uint16_t current_dir_cluster, const uint8_t* data_buffer, uint32_t data_size) {
        uint8_t sector_buf[512];
        uint32_t target_sector = 0;
        uint32_t sectors_to_read = 0;

        // 1. Resolve where the directory listing resides to find our file entry later
        if (current_dir_cluster == 0) {
            target_sector = root_dir_sector;
            sectors_to_read = (active_bpb.root_entries * 32) / 512;
        } else {
            target_sector = data_sector + ((current_dir_cluster - 2) * active_bpb.sectors_per_cluster);
            sectors_to_read = active_bpb.sectors_per_cluster;
        }

        uint8_t formatted_name[11];
        format_raw_name_to_fat(name, formatted_name);

        // 2. Calculate how many clusters we actually need to allocate
        uint32_t bytes_per_cluster = active_bpb.sectors_per_cluster * 512;
        uint32_t clusters_needed = (data_size + bytes_per_cluster - 1) / bytes_per_cluster;
        if (clusters_needed == 0 && data_size > 0) clusters_needed = 1;

        uint16_t first_allocated_cluster = 0;
        uint16_t previous_cluster = 0;
        uint32_t bytes_written = 0;

        // 3. Allocate clusters in the FAT chain and write the data blocks
        if (clusters_needed > 0) {
            uint8_t fat_sector[512];
            uint32_t current_fat_sector = 0xFFFFFFFF;

            for (uint32_t c = 0; c < clusters_needed; c++) {
                // Find a free cluster (0x0000) by scanning the File Allocation Table
                uint16_t free_cluster = 0;
                uint32_t total_fat_entries = active_bpb.sectors_per_fat * 256; // 256 entries of 2 bytes per sector

                for (uint32_t entry = 2; entry < total_fat_entries; entry++) {
                    uint32_t byte_offset = entry * 2;
                    uint32_t sector_offset = active_bpb.reserved_sectors + (byte_offset / 512);
                    uint32_t byte_inside = byte_offset % 512;

                    if (sector_offset != current_fat_sector) {
                        if (current_fat_sector != 0xFFFFFFFF) {
                            Disk::write_sector(current_fat_sector, fat_sector);
                            Disk::write_sector(current_fat_sector + active_bpb.sectors_per_fat, fat_sector);
                        }
                        current_fat_sector = sector_offset;
                        Disk::read_sector(current_fat_sector, fat_sector);
                    }

                    if (*(uint16_t*)&fat_sector[byte_inside] == 0x0000) {
                        free_cluster = entry;
                        // Temporarily mark as allocated end of chain so nothing else grabs it
                        *(uint16_t*)&fat_sector[byte_inside] = 0xFFFF;
                        Disk::write_sector(current_fat_sector, fat_sector);
                        Disk::write_sector(current_fat_sector + active_bpb.sectors_per_fat, fat_sector);
                        break;
                    }
                }

                if (free_cluster == 0) return false; // Disk is completely full!

                if (first_allocated_cluster == 0) {
                    first_allocated_cluster = free_cluster;
                } else {
                    // Link previous cluster to this new cluster in the FAT chain
                    uint32_t prev_byte_offset = previous_cluster * 2;
                    uint32_t prev_sector_offset = active_bpb.reserved_sectors + (prev_byte_offset / 512);
                    uint32_t prev_byte_inside = prev_byte_offset % 512;
                    
                    uint8_t link_buf[512];
                    Disk::read_sector(prev_sector_offset, link_buf);
                    *(uint16_t*)&link_buf[prev_byte_inside] = free_cluster;
                    Disk::write_sector(prev_sector_offset, link_buf);
                    Disk::write_sector(prev_sector_offset + active_bpb.sectors_per_fat, link_buf);
                }

                previous_cluster = free_cluster;

                // Write actual data payload bytes onto the data sectors mapped by this cluster
                uint32_t cluster_sector_start = data_sector + ((free_cluster - 2) * active_bpb.sectors_per_cluster);
                for (uint8_t s = 0; s < active_bpb.sectors_per_cluster; s++) {
                    uint8_t write_buf[512];
                    for (int b = 0; b < 512; b++) {
                        if (bytes_written < data_size) {
                            write_buf[b] = data_buffer[bytes_written++];
                        } else {
                            write_buf[b] = 0x00; // Pad trailing empty buffer spaces
                        }
                    }
                    if (!Disk::write_sector(cluster_sector_start + s, write_buf)) return false;
                }
            }
        }

        // 4. Update the directory entry with the starting cluster and file size record
        for (uint32_t s = 0; s < sectors_to_read; s++) {
            if (!Disk::read_sector(target_sector + s, sector_buf)) return false;

            FAT_Dir_Entry* entries = (FAT_Dir_Entry*)sector_buf;

            for (int e = 0; e < 16; e++) {
                // Match the exact file tracking block
                bool match = true;
                for (int i = 0; i < 8; i++) if (entries[e].name[i] != formatted_name[i]) match = false;
                for (int i = 0; i < 3; i++) if (entries[e].ext[i] != formatted_name[8 + i]) match = false;

                if (match && !(entries[e].attr & 0x10)) { // Verify it's a file, not a directory
                    entries[e].cluster_low = first_allocated_cluster;
                    entries[e].size = data_size;

                    // Flush updated tracking sizing parameters back to disk storage
                    return Disk::write_sector(target_sector + s, sector_buf);
                }
            }
        }

        return false;
    }
    // Dynamic payload stream read engine
    int read_file_data(const char* name, uint16_t current_dir_cluster, uint8_t* output_buffer) {
        uint8_t sector_buf[512];
        uint32_t target_sector = 0;
        uint32_t sectors_to_read = 0;

        // 1. Resolve where the directory listing resides to find our file entry
        if (current_dir_cluster == 0) {
            target_sector = root_dir_sector;
            sectors_to_read = (active_bpb.root_entries * 32) / 512;
        } else {
            target_sector = data_sector + ((current_dir_cluster - 2) * active_bpb.sectors_per_cluster);
            sectors_to_read = active_bpb.sectors_per_cluster;
        }

        uint8_t formatted_name[11];
        format_raw_name_to_fat(name, formatted_name);

        uint16_t current_cluster = 0;
        uint32_t file_size = 0;
        bool file_found = false;

        // 2. Scan directory to grab the file entry tracking metrics
        for (uint32_t s = 0; s < sectors_to_read; s++) {
            if (!Disk::read_sector(target_sector + s, sector_buf)) return -1;

            FAT_Dir_Entry* entries = (FAT_Dir_Entry*)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (entries[e].name[0] == 0x00) return -1; // End of list
                if ((uint8_t)entries[e].name[0] == 0xE5) continue; // Deleted

                bool match = true;
                for (int i = 0; i < 8; i++) if (entries[e].name[i] != formatted_name[i]) match = false;
                for (int i = 0; i < 3; i++) if (entries[e].ext[i] != formatted_name[8 + i]) match = false;

                if (match && !(entries[e].attr & 0x10)) { // Must be a file, not folder
                    current_cluster = entries[e].cluster_low;
                    file_size = entries[e].size;
                    file_found = true;
                    break;
                }
            }
            if (file_found) break;
        }

        if (!file_found) return -1; // File not found
        if (file_size == 0 || current_cluster == 0) return 0; // Empty file placeholder

        // 3. Walk the FAT cluster chain and ingest the data payload blocks
        uint32_t bytes_read = 0;
        uint32_t fat_start_sector = active_bpb.reserved_sectors;

        while (current_cluster >= 2 && current_cluster < 0xFFF8 && bytes_read < file_size) {
            uint32_t cluster_sector_start = data_sector + ((current_cluster - 2) * active_bpb.sectors_per_cluster);
            
            // Ingest all sectors compiled within this specific cluster bundle
            for (uint8_t s = 0; s < active_bpb.sectors_per_cluster; s++) {
                uint8_t cluster_buf[512];
                if (!Disk::read_sector(cluster_sector_start + s, cluster_buf)) return -1;

                for (int b = 0; b < 512; b++) {
                    if (bytes_read < file_size) {
                        output_buffer[bytes_read++] = cluster_buf[b];
                    } else {
                        break;
                    }
                }
            }

            // Move cursor to the next linked node mapped by the Allocation Tables
            uint32_t byte_offset = current_cluster * 2;
            uint32_t sector_offset = fat_start_sector + (byte_offset / 512);
            uint32_t byte_inside = byte_offset % 512;

            uint8_t fat_sector[512];
            if (!Disk::read_sector(sector_offset, fat_sector)) return -1;

            current_cluster = *(uint16_t*)&fat_sector[byte_inside];
        }

        return bytes_read; // Return total read count back to application workspace
    }
    // Reads a directory and pipes entries to an external callback function for UI caching
    void list_directory_to_cache(uint16_t current_dir_cluster, void (*callback)(const char*, bool, uint16_t, uint32_t)) {
        uint8_t sector_buf[512];
        uint32_t target_sector = 0;
        uint32_t sectors_to_read = 0;

        // Dynamic targeting based on execution depth mapping (Root directory vs subfolder)
        if (current_dir_cluster == 0) {
            target_sector = root_dir_sector;
            sectors_to_read = (active_bpb.root_entries * 32) / 512;
        } else {
            target_sector = data_sector + ((current_dir_cluster - 2) * active_bpb.sectors_per_cluster);
            sectors_to_read = active_bpb.sectors_per_cluster;
        }

        for (uint32_t s = 0; s < sectors_to_read; s++) {
            if (!Disk::read_sector(target_sector + s, sector_buf)) {
                return; // Silently fail on raw sector reads
            }

            FAT_Dir_Entry* entries = (FAT_Dir_Entry*)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (entries[e].name[0] == 0x00) {
                    return; 
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) continue; 
                if (entries[e].attr == 0x0F) continue;             

                char clean_name[13];
                parse_fat_name(&entries[e], clean_name);

                // ------------------------------------------------------------
                // CRITICAL HIDE FILTER: Do not push dot entries to the TUI Cache
                // ------------------------------------------------------------
                if (strcmp_simple(clean_name, ".") || strcmp_simple(clean_name, "..")) {
                    continue; 
                }

                bool is_dir = (entries[e].attr & 0x10) != 0;
                uint16_t cluster_target = entries[e].cluster_low;
                uint32_t entry_size = entries[e].size;

                if (callback != nullptr) {
                    callback(clean_name, is_dir, cluster_target, entry_size);
                }
            }
        }
    }
    // Helper to look up an unallocated entry in the FAT16 table
    static uint16_t find_free_cluster() {
        uint8_t fat_sector[512];
        uint32_t total_fat_entries = active_bpb.sectors_per_fat * 256; 
        uint32_t fat_start_sector = active_bpb.reserved_sectors;

        for (uint32_t entry = 2; entry < total_fat_entries; entry++) {
            uint32_t byte_offset = entry * 2;
            uint32_t sector_offset = fat_start_sector + (byte_offset / 512);
            uint32_t byte_inside = byte_offset % 512;

            if (!Disk::read_sector(sector_offset, fat_sector)) return 0;

            if (*(uint16_t*)&fat_sector[byte_inside] == 0x0000) {
                // Found a free cluster slot
                return entry;
            }
        }
        return 0; // Disk Full
    }
}
