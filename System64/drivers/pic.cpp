#include "pic.hpp"

static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
void pic_clear_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8)
        port = 0x21;
    else {
        port = 0xA1;
        irq -= 8;
    }

    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
        outb(0xA0, 0x20);

    outb(0x20, 0x20);
}

void pic_remap()
{
    // Start initialization
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    // Set vector offsets
    outb(0x21, 0x20); // Master offset 32
    outb(0xA1, 0x28); // Slave offset 40

    // Tell Master about Slave at IRQ2
    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    // 8086 mode
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // MASK ALL IRQs initially
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}