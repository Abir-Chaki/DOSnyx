#include <stdint.h>
#include "fs.hpp"
#include "memory/heap.hpp"
#include "memory/pmm.hpp"
#include "drivers/idt.hpp"
#include "drivers/pic.hpp"
#include "drivers/keyboard.hpp"

struct interrupt_frame {
    uint64_t r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code, rip, cs, rflags, rsp, ss;
};

// Externs for Notepad and Kernel Entry
extern void notepad(Node* file_node);
extern "C" void kernel_main();

/* ================= VGA & UI GLOBALS ================= */
static const int VGA_WIDTH  = 80;
static const int VGA_HEIGHT = 25;
volatile uint16_t* vga = (uint16_t*)0xB8000;
extern "C" volatile uint64_t timer_ticks = 0;

uint8_t color = 0x0F;
int row = 0;
int col = 0;
char input_buffer[256];
int input_length = 0;
int prompt_start_col = 0;
char username[32] = "nyx authority/dos";

/* ================= FILE SYSTEM GLOBALS ================= */
Node* all_nodes_head = nullptr; 
Node* current_dir_ptr = nullptr;

/* ================= UTILITY FUNCTIONS ================= */

void kstrncpy(char* dest, const char* src, int n) {
    int i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    dest[i] = '\0';
}

bool strcmp_simple(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t value) {
    asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

void update_cursor() {
    uint16_t pos = row * VGA_WIDTH + col;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void scroll() {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) vga[i] = vga[i + VGA_WIDTH];
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) 
        vga[i] = (color << 8) | ' ';
    row = VGA_HEIGHT - 1;
}

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = (color << 8) | ' ';
    row = 0; col = 0;
    update_cursor();
}

extern "C" void putchar(char c) {
    if (c == '\n') { row++; col = 0; } 
    else {
        vga[row * VGA_WIDTH + col] = (color << 8) | c;
        col++;
        if (col >= VGA_WIDTH) { col = 0; row++; }
    }
    if (row >= VGA_HEIGHT) scroll();
    update_cursor();
}

extern "C" void print(const char* str) {
    while (*str) putchar(*str++);
}

/* ================= FROM BACKUP: CURSOR ================= */
void enable_cursor(uint8_t start, uint8_t end)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | start);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | end);
}

/* ================= FROM BACKUP: SPLASH ================= */
void splash_screen()
{
    color = 0x1F; // Blue background, White text
    clear_screen();
    print("DOSnyx Operating System\n");
    print("Version 2.6\n\n");
    print("Press any key to continue...");
    
    // This waits for a hardware interrupt from the keyboard
    while (!keyboard_getchar()); 
    
    color = 0x0F; // Back to Black background, White text
    clear_screen();
}

void print_hex(uint64_t val) {
    const char* hex = "0123456789ABCDEF";
    print("0x");
    for (int i = 60; i >= 0; i -= 4) putchar(hex[(val >> i) & 0xF]);
}

void hard_reboot() {
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    while (true) asm volatile ("hlt");
}

/* ================= DYNAMIC FS CORE ================= */

void fs_init() {
    print("Allocating Root... ");
    //asm volatile("hlt");
    Node* root = new Node();
    //asm volatile("hlt");
    
    if (root == nullptr) {
        color = 0x1F; // Blue
        print("CRITICAL: Heap returned NULL for Root Node!\n");
        while(1) asm("hlt");
    }
    //asm volatile("hlt");

    // Print the address to see where the heap is putting things
    print("Address: ");
    print_hex((uint64_t)root);
    putchar('\n');
    //asm volatile("hlt");

    kstrncpy(root->name, "root", 15);
    root->is_folder = true;
    root->parent = nullptr;
    root->next = nullptr;
    root->size = 0;
    
    all_nodes_head = root;
    current_dir_ptr = root;
}

Node* fs_find(const char* name) {
    Node* curr = all_nodes_head;
    while (curr != nullptr) {
        if (curr->parent == current_dir_ptr && strcmp_simple(curr->name, name))
            return curr;
        curr = curr->next;
    }
    return nullptr;
}

Node* fs_create(const char* name, bool folder) {
    Node* newNode = new Node();
    kstrncpy(newNode->name, name, 15);
    newNode->is_folder = folder;
    newNode->parent = current_dir_ptr;
    newNode->size = 0;
    newNode->content[0] = 0; // Initialize first byte
    
    newNode->next = all_nodes_head;
    all_nodes_head = newNode;
    return newNode;
}

void fs_delete(const char* name) {
    Node* curr = all_nodes_head;
    Node* prev = nullptr;
    while (curr != nullptr) {
        if (curr->parent == current_dir_ptr && strcmp_simple(curr->name, name)) {
            if (prev == nullptr) all_nodes_head = curr->next;
            else prev->next = curr->next;
            delete curr;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/* ================= SHELL UI ================= */

void print_path(Node* dir) {
    if (dir == nullptr || dir->parent == nullptr) return; 
    print_path(dir->parent);
    print(dir->name);
    print("/");
}

void print_prompt() {
    if (col != 0) putchar('\n');
    print("1:/");
    print_path(current_dir_ptr);
    print("> ");
    prompt_start_col = col;
    input_length = 0;
}

void show_cmds() {
    print("ver - Version Info\nabout - About DOSnyx\ncl - Clear screen\nrst - Reboot\n");
    print("dir - List files\ncreate <f> - New file\ndel <f> - Delete\nmf <f> - New folder\n");
    print("cf <f> - Change folder\nusrnm - Current user\nwrite <f> - Notepad\n\n");
}

void handle_enter() {
    input_buffer[input_length] = 0;
    putchar('\n');

    if (strcmp_simple(input_buffer, "ver")) {
        print("DOSnyx Version 2.6\n\n");
    }
    else if (strcmp_simple(input_buffer, "about")) {
        print("DOSnyx: Hybrid Kernel Concept\nBy Abir Chaki | 13 y/o Dev | India\n\n");
    }
    else if (strcmp_simple(input_buffer, "cl")) {
        clear_screen(); print_prompt(); return;
    }
    else if (strcmp_simple(input_buffer, "rst")) {
        hard_reboot();
    }
    else if (strcmp_simple(input_buffer, "usrnm")) {
        print(username); print("\n\n");
    }
    else if (strcmp_simple(input_buffer, "cmds")) {
        show_cmds();
    }
    else if (strcmp_simple(input_buffer, "dir")) {
        Node* curr = all_nodes_head;
        while (curr != nullptr) {
            if (curr->parent == current_dir_ptr) {
                print(curr->is_folder ? "[DIR]  " : "[FILE] ");
                print(curr->name); putchar('\n');
            }
            curr = curr->next;
        }
        putchar('\n');
    }
    else if (input_length > 7 && input_buffer[0]=='c' && input_buffer[1]=='r') {
        fs_create(&input_buffer[7], false);
        print("File created.\n\n");
    }
    else if (input_length > 4 && input_buffer[0]=='d' && input_buffer[1]=='e') {
        fs_delete(&input_buffer[4]);
        print("Deleted.\n\n");
    }
    else if (input_length > 3 && input_buffer[0]=='m' && input_buffer[1]=='f') {
        fs_create(&input_buffer[3], true);
        print("Folder created.\n\n");
    }
    else if (input_length > 3 && input_buffer[0]=='c' && input_buffer[1]=='f') {
        if (input_buffer[3]=='.' && input_buffer[4]=='.') {
            if (current_dir_ptr->parent != nullptr) current_dir_ptr = current_dir_ptr->parent;
        } else {
            Node* target = fs_find(&input_buffer[3]);
            if (target && target->is_folder) current_dir_ptr = target;
            else print("Folder not found.\n\n");
        }
    }
    else if (input_length > 8 && input_buffer[0]=='p' && input_buffer[1]=='r' && input_buffer[2]=='n') {
        print(&input_buffer[8]); print("\n\n");
    }
    else if (input_length > 6 && input_buffer[0]=='w' && input_buffer[1]=='r') {
        Node* target = fs_find(&input_buffer[6]);
        if (!target) target = fs_create(&input_buffer[6], false);
        clear_screen();
        notepad(target);
    }
    else if (strcmp_simple(input_buffer, "sht")) {
        clear_screen();
        color = 0x0E; // Yellow
        print("Shutting down\n");

        // Simple busy-wait (doesn't need the timer/interrupts)
        // Adjust the number of zeros to change the delay time
        for(volatile uint64_t i = 0; i < 900000000; i++) {
            asm volatile("nop"); 
        }

//        // QEMU/Bochs Shutdown signals
        outw(0xB004, 0x2000);
        outw(0x604, 0x2000);
        outw(0x4004, 0x3400);

//       // If we reach here, hardware shutdown failed
        color = 0x1F; 
        print("\nIt is now safe to turn off the computer.\n");
        while(1) asm("hlt");
    }
    else if (input_length > 0) print("Unknown command\n\n");

    print_prompt();
}

/* ================= KERNEL START ================= */

extern "C" void kernel_main() {
    pmm_init(128 * 1024 * 1024, 0x180000);
    for (uint64_t addr = 0x200000; addr < 128 * 1024 * 1024; addr += 4096) pmm_mark_free(addr);

    idt_init();
    pic_remap();
    heap_init();
    
    extern void pic_clear_mask(uint8_t irq);
    pic_clear_mask(1); // Keyboard
    
    asm volatile ("sti");

    fs_init();
    enable_cursor(1,15);
    splash_screen();
    clear_screen();
    //print("DOSnyx v2.6 Online\n");
    print_prompt();

    while (true) {
        char c = keyboard_getchar();
        if (!c) continue;
        if (c == '\n') handle_enter();
        else if (c == '\b') {
            if (input_length > 0 && col > prompt_start_col) { 
                input_length--; col--; 
                vga[row * VGA_WIDTH + col] = (color << 8) | ' '; 
                update_cursor(); 
            }
        }
        else if (input_length < 255) { input_buffer[input_length++] = c; putchar(c); }
    }
}

extern "C" void isr_dispatch(interrupt_frame* frame) {
    color = 0x4F; clear_screen();
    print("==== KERNEL PANIC ====\n");
    print("Int: "); print_hex(frame->int_no);
    print("\nRIP: "); print_hex(frame->rip);
    while (1) asm("hlt");
}

/* ================= INTERRUPT HANDLERS ================= */

//extern "C" uint64_t timer_ticks; // Ensure this is accessible

extern "C" void isr_timer()
{
    timer_ticks++;
    pic_send_eoi(0); // Tell the PIC we handled the timer interrupt
}