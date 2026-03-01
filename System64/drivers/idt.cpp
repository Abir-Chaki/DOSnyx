#include "idt.hpp"

static IDTEntry idt[256];
static IDTPointer idt_ptr;

extern "C" void load_idt(uint64_t);

extern "C" {
    void isr0();  void isr1();  void isr2();  void isr3();
    void isr4();  void isr5();  void isr6();  void isr7();
    void isr8();  void isr9();  void isr10(); void isr11();
    void isr12(); void isr13(); void isr14(); void isr15();
    void isr16(); void isr17(); void isr18(); void isr19();
    void isr20(); void isr21(); void isr22(); void isr23();
    void isr24(); void isr25(); void isr26(); void isr27();
    void isr28(); void isr29(); void isr30(); void isr31();

    void irq0_handler();
    void irq1_handler();
}

static void set_idt_gate(int n, uint64_t handler)
{
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;
    idt[n].ist         = 0;
    idt[n].type_attr   = 0x8E;
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32);
    idt[n].zero        = 0;
}

extern "C" void idt_init()
{
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)&idt;

    set_idt_gate(0,  (uint64_t)isr0);
    set_idt_gate(1,  (uint64_t)isr1);
    set_idt_gate(2,  (uint64_t)isr2);
    set_idt_gate(3,  (uint64_t)isr3);
    set_idt_gate(4,  (uint64_t)isr4);
    set_idt_gate(5,  (uint64_t)isr5);
    set_idt_gate(6,  (uint64_t)isr6);
    set_idt_gate(7,  (uint64_t)isr7);
    set_idt_gate(8,  (uint64_t)isr8);
    set_idt_gate(9,  (uint64_t)isr9);
    set_idt_gate(10, (uint64_t)isr10);
    set_idt_gate(11, (uint64_t)isr11);
    set_idt_gate(12, (uint64_t)isr12);
    set_idt_gate(13, (uint64_t)isr13);
    set_idt_gate(14, (uint64_t)isr14);
    set_idt_gate(15, (uint64_t)isr15);
    set_idt_gate(16, (uint64_t)isr16);
    set_idt_gate(17, (uint64_t)isr17);
    set_idt_gate(18, (uint64_t)isr18);
    set_idt_gate(19, (uint64_t)isr19);
    set_idt_gate(20, (uint64_t)isr20);
    set_idt_gate(21, (uint64_t)isr21);
    set_idt_gate(22, (uint64_t)isr22);
    set_idt_gate(23, (uint64_t)isr23);
    set_idt_gate(24, (uint64_t)isr24);
    set_idt_gate(25, (uint64_t)isr25);
    set_idt_gate(26, (uint64_t)isr26);
    set_idt_gate(27, (uint64_t)isr27);
    set_idt_gate(28, (uint64_t)isr28);
    set_idt_gate(29, (uint64_t)isr29);
    set_idt_gate(30, (uint64_t)isr30);
    set_idt_gate(31, (uint64_t)isr31);

    set_idt_gate(32, (uint64_t)irq0_handler);
    set_idt_gate(33, (uint64_t)irq1_handler);

    load_idt((uint64_t)&idt_ptr);
}