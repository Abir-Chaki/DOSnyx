#ifndef DISK_HPP
#define DISK_HPP

#include <stdint.h>

namespace Disk {
    // ATA Register I/O Port Offsets (added to dynamic base: 0x1F0 or 0x170)
    constexpr uint16_t REG_OFFSET_DATA     = 0;
    constexpr uint16_t REG_OFFSET_FEATURES = 1;
    constexpr uint16_t REG_OFFSET_SECCOUNT = 2;
    constexpr uint16_t REG_OFFSET_LBA_LO   = 3;
    constexpr uint16_t REG_OFFSET_LBA_MID  = 4;
    constexpr uint16_t REG_OFFSET_LBA_HI   = 5;
    constexpr uint16_t REG_OFFSET_DRVSEL   = 6;
    constexpr uint16_t REG_OFFSET_COMMAND  = 7;
    constexpr uint16_t REG_OFFSET_STATUS   = 7;

    // ATA Command Codes
    constexpr uint8_t ATA_CMD_READ_PIO   = 0x20;
    constexpr uint8_t ATA_CMD_WRITE_PIO  = 0x30;
    constexpr uint8_t ATA_CMD_IDENTIFY   = 0xEC;
    constexpr uint8_t ATA_CMD_CACHE_FLUSH = 0xE7;

    // ATA Status Register Flags
    constexpr uint8_t ATA_SR_BSY         = 0x80;
    constexpr uint8_t ATA_SR_DRQ         = 0x08;
    constexpr uint8_t ATA_SR_ERR         = 0x01;

    // Initialization and Verification
    bool init();

    // Low-Level Block I/O Operations (Dynamic Multi-Drive Tracking)
    bool read_sector(uint32_t lba, uint8_t* buffer);
    bool write_sector(uint32_t lba, const uint8_t* buffer);
}

#endif