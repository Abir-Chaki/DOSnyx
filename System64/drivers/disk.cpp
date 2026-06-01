#include "disk.hpp"
#include "io.hpp" // Pulls inline outb, inb, outw, and inw

// Links directly to your shell/VFS active drive index variable
extern int current_drive_id; 

namespace Disk {

    // Helper function to dynamically resolve the base I/O port and drive select bit
    static void get_drive_hardware_routing(uint16_t& out_base_port, uint8_t& out_drv_bit) {
        // Mappings adjusted to your production drive setup:
        // 1: -> dosnyx_disk.img  (Primary Master)  -> Native NyxFS
        // 2: -> dosnyx_disk2.img (Primary Slave)   -> FAT16 Storage
        // 3: -> dosnyx_disk3.img (Secondary Master) -> FAT16 Storage
        // 4: -> dosnyx_disk4.img (Secondary Slave)  -> FAT16 Storage

        switch (current_drive_id) {
            case 1: // Drive 1: Primary Master
                out_base_port = 0x1F0; 
                out_drv_bit   = 0xE0; 
                break;
            case 2: // Drive 2: Primary Slave
                out_base_port = 0x1F0; 
                out_drv_bit   = 0xF0; 
                break;
            case 3: // Drive 3: Secondary Master
                out_base_port = 0x170; 
                out_drv_bit   = 0xE0; 
                break;
            case 4: // Drive 4: Secondary Slave
                out_base_port = 0x170; 
                out_drv_bit   = 0xF0; 
                break;
            default: // Robust fallback safe state
                out_base_port = 0x1F0; 
                out_drv_bit   = 0xE0; 
                break;
        }
    }

    static void wait_ready(uint16_t base_port) {
        while (inb(base_port + REG_OFFSET_STATUS) & ATA_SR_BSY) {
            asm volatile("pause");
        }
    }

    static bool wait_drq(uint16_t base_port) {
        for (int i = 0; i < 100000; i++) {
            uint8_t status = inb(base_port + REG_OFFSET_STATUS);
            if (status & ATA_SR_ERR) return false;
            if (status & ATA_SR_DRQ) return true;
            asm volatile("pause");
        }
        return false;
    }

    bool init() {
        // Initialize all valid controller targets to probe for responsive drives
        uint16_t ports[2] = { 0x1F0, 0x170 };
        uint8_t heads[2]  = { 0xA0, 0xB0 }; 

        for (int p = 0; p < 2; p++) {
            for (int h = 0; h < 2; h++) {
                outb(ports[p] + REG_OFFSET_DRVSEL, heads[h]);
                outb(ports[p] + REG_OFFSET_SECCOUNT, 0);
                outb(ports[p] + REG_OFFSET_LBA_LO, 0);
                outb(ports[p] + REG_OFFSET_LBA_MID, 0);
                outb(ports[p] + REG_OFFSET_LBA_HI, 0);
                
                outb(ports[p] + REG_OFFSET_COMMAND, ATA_CMD_IDENTIFY);
                
                uint8_t status = inb(ports[p] + REG_OFFSET_STATUS);
                if (status == 0) continue; // No hardware connected to this port line

                wait_ready(ports[p]);
                if (wait_drq(ports[p])) {
                    // Drain the 512-byte identity sector data safely out of data register
                    for (int i = 0; i < 256; i++) {
                        inw(ports[p] + REG_OFFSET_DATA);
                    }
                }
            }
        }
        return true;
    }

    bool read_sector(uint32_t lba, uint8_t* buffer) {
        uint16_t base_port;
        uint8_t drv_bit;
        get_drive_hardware_routing(base_port, drv_bit);

        wait_ready(base_port);

        // Send drive head select alongside high 4 bits of the LBA address
        outb(base_port + REG_OFFSET_DRVSEL, drv_bit | ((lba >> 24) & 0x0F));
        outb(base_port + REG_OFFSET_SECCOUNT, 1);
        
        // Pass standard dynamic low level byte streams
        outb(base_port + REG_OFFSET_LBA_LO,   (uint8_t)(lba & 0xFF));
        outb(base_port + REG_OFFSET_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(base_port + REG_OFFSET_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));

        outb(base_port + REG_OFFSET_COMMAND,  ATA_CMD_READ_PIO);

        if (!wait_drq(base_port)) return false;

        uint16_t* ptr = (uint16_t*)buffer;
        for (int i = 0; i < 256; i++) {
            ptr[i] = inw(base_port + REG_OFFSET_DATA);
        }

        return true;
    }

    bool write_sector(uint32_t lba, const uint8_t* buffer) {
        uint16_t base_port;
        uint8_t drv_bit;
        get_drive_hardware_routing(base_port, drv_bit);

        wait_ready(base_port);

        outb(base_port + REG_OFFSET_DRVSEL, drv_bit | ((lba >> 24) & 0x0F));
        outb(base_port + REG_OFFSET_SECCOUNT, 1);
        
        outb(base_port + REG_OFFSET_LBA_LO,   (uint8_t)(lba & 0xFF));
        outb(base_port + REG_OFFSET_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
        outb(base_port + REG_OFFSET_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));

        outb(base_port + REG_OFFSET_COMMAND,  ATA_CMD_WRITE_PIO);

        if (!wait_drq(base_port)) return false;

        const uint16_t* ptr = (const uint16_t*)buffer;
        for (int i = 0; i < 256; i++) {
            outw(base_port + REG_OFFSET_DATA, ptr[i]);
        }

        // Send cache flush command right to the dynamic command bus offset
        outb(base_port + REG_OFFSET_COMMAND, ATA_CMD_CACHE_FLUSH);
        wait_ready(base_port);

        return true;
    }
}