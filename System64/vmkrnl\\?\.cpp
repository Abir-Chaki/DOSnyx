#include <stdint.h>
#include "fs.hpp"
#include "memory/heap.hpp"
#include "memory/pmm.hpp"
#include "drivers/idt.hpp"
#include "drivers/pic.hpp"
#include "drivers/keyboard.hpp"

// Updated to include CPU-pushed registers for better Panic reporting

struct interrupt_frame
{
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx;
    uint64_t rdx, rcx, rax;
    uint64_t int_no;
    uint64_t err_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

extern void notepad(const char* filename);
extern "C" void kernel_main();

/* ================= VGA ================= */

static const int VGA_WIDTH  = 80;
static const int VGA_HEIGHT = 25;

volatile uint16_t* vga = (uint16_t*)0xB8000;
volatile uint64_t timer_ticks = 0;

uint8_t color = 0x0F;
int row = 0;
int col = 0;

bool shift_pressed = false;
char input_buffer[256];
int input_length = 0;
int prompt_start_col = 0;

char username[32] = "nyx authority/dos";

/* ================= FILE SYSTEM ================= */

Node nodes[MAX_NODES];
int current_dir = 0;

/* ================= PORT I/O ================= */

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* ================= CURSOR ================= */

void enable_cursor(uint8_t start, uint8_t end)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | start);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | end);
}

void update_cursor()
{
    uint16_t pos = row * VGA_WIDTH + col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* ================= SCREEN ================= */

void scroll()
{
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        vga[i] = vga[i + VGA_WIDTH];

    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
        vga[i] = (color << 8) | ' ';

    row = VGA_HEIGHT - 1;
}

void clear_screen()
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (color << 8) | ' ';

    row = 0;
    col = 0;
    update_cursor();
}

extern "C" void putchar(char c)
{
    if (c == '\n') {
        row++;
        col = 0;
    } else {
        vga[row * VGA_WIDTH + col] = (color << 8) | c;
        col++;
        if (col >= VGA_WIDTH) {
            col = 0;
            row++;
        }
    }

    if (row >= VGA_HEIGHT)
        scroll();

    update_cursor();
}

void hard_reboot()
{
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    while (true) { asm volatile ("hlt"); }
}

extern "C" void print(const char* str)
{
    while (*str)
        putchar(*str++);
}


/* ================= HELPERS ================= */

bool strcmp_simple(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

void print_hex(uint64_t val)
{
    const char* hex = "0123456789ABCDEF";
    print("0x");
    for (int i = 60; i >= 0; i -= 4)
        putchar(hex[(val >> i) & 0xF]);
}

class Vector3 {
public:
    int x, y, z;
    Vector3(int _x, int _y, int _z) : x(_x), y(_y), z(_z) {
        // This confirms constructors are called
    }
};

void test_new_operator() {
    print("Testing C++ new... ");
    
    Vector3* v = new Vector3(10, 20, 30);

    if (v->x == 10 && v->z == 30) {
        print("SUCCESS! Value: ");
        print_hex(v->y); // Should show 0x14 (20)
        putchar('\n');
    } else {
        print("FAILED!\n");
    }

    delete v; // This calls your (currently empty) kfree
    asm volatile("hlt");
}

/* ================= SPLASH ================= */

void splash_screen()
{
    color = 0x1F;
    clear_screen();
    print("DOSnyx Operating System\n");
    print("Version 2.5\n\n");
    print("Press any key to continue...");
    while (!keyboard_getchar());
    color = 0x0F;
    clear_screen();
}

/* ================= FILE SYSTEM ================= */

void fs_init()
{
    for (int i = 0; i < MAX_NODES; i++)
        nodes[i].used = false;

    nodes[0].used = true;
    nodes[0].is_folder = true;
    nodes[0].parent = -1;
    nodes[0].name[0] = 0;
}

int fs_find(const char* name)
{
    for (int i = 0; i < MAX_NODES; i++)
        if (nodes[i].used &&
            nodes[i].parent == current_dir &&
            strcmp_simple(nodes[i].name, name))
            return i;
    return -1;
}

int fs_create(const char* name, bool folder)
{
    for (int i = 0; i < MAX_NODES; i++)
        if (!nodes[i].used)
        {
            nodes[i].used = true;
            nodes[i].is_folder = folder;
            nodes[i].parent = current_dir;
            int j = 0;
            while (name[j] && j < 15) {
                nodes[i].name[j] = name[j];
                j++;
            }
            nodes[i].name[j] = 0;
            nodes[i].size = 0;
            return i;
        }
    return -1;
}

void fs_delete(const char* name)
{
    int idx = fs_find(name);
    if (idx >= 0)
        nodes[idx].used = false;
}

/* ================= PROMPT ================= */

void print_path(int dir)
{
    if (dir == -1) return;
    print_path(nodes[dir].parent);
    if (dir != 0) {
        print(nodes[dir].name);
        print("/");
    }
}

void print_prompt()
{
    if (col != 0) putchar('\n');
    print("1:/");
    print_path(current_dir);
    print("> ");
    prompt_start_col = col;
    input_length = 0;
}

/* ================= COMMAND HANDLING ================= */

void show_cmds()
{
    print("ver - Current Version\n");
    print("about - About DOSnyx\n");
    print("cl - Clear Console\n");
    print("rst - Reboot\n");
    print("dir - List files\n");
    print("create <file> - Create a file\n");
    print("del <name> - Delete a file/folder\n");
    print("mf <folder> - Make folder\n");
    print("cf <folder> - Change folder\n");
    print("prnline <text> - Output text\n");
    print("usrnm - Current Username\n");
    print("write <file> - Open Notepad\n");
    print("cmds - Help menu\n\n");
}

void handle_enter()
{
    input_buffer[input_length] = 0;
    putchar('\n');

    // ver
    if (strcmp_simple(input_buffer, "ver"))
        print("DOSnyx Version 2.5\n\n");

    // about
    else if (strcmp_simple(input_buffer, "about"))
        print("DOSnyx: Experimental Hybrid Kernel Concept\n\n");

    // cl
    else if (strcmp_simple(input_buffer, "cl")) {
        clear_screen();
        print_prompt();
        return;
    }

    // rst
    else if (strcmp_simple(input_buffer, "rst"))
        hard_reboot();

    // dir
    else if (strcmp_simple(input_buffer, "dir")) {
        for (int i = 0; i < MAX_NODES; i++)
            if (nodes[i].used && nodes[i].parent == current_dir) {
                print(nodes[i].is_folder ? "[DIR] " : "[FILE] ");
                print(nodes[i].name);
                putchar('\n');
            }
        putchar('\n');
    }

    // usrnm
    else if (strcmp_simple(input_buffer, "usrnm")) {
        print(username);
        print("\n\n");
    }

    // cmds
    else if (strcmp_simple(input_buffer, "cmds"))
        show_cmds();

    // create <file>
    else if (input_length > 7 && input_buffer[0]=='c' && input_buffer[1]=='r' && input_buffer[2]=='e') {
        fs_create(&input_buffer[7], false);
        print("File created\n\n");
    }

    // del <name>
    else if (input_length > 4 && input_buffer[0]=='d' && input_buffer[1]=='e' && input_buffer[2]=='l') {
        fs_delete(&input_buffer[4]);
        print("Deleted\n\n");
    }

    // mf <folder>
    else if (input_length > 3 && input_buffer[0]=='m' && input_buffer[1]=='f' && input_buffer[2]==' ') {
        fs_create(&input_buffer[3], true);
        print("Folder created\n\n");
    }

    // cf <folder>
    else if (input_length > 3 && input_buffer[0]=='c' && input_buffer[1]=='f' && input_buffer[2]==' ') {
        if (input_buffer[3]=='.' && input_buffer[4]=='.') {
            if (nodes[current_dir].parent != -1)
                current_dir = nodes[current_dir].parent;
        } else {
            int idx = fs_find(&input_buffer[3]);
            if (idx >= 0 && nodes[idx].is_folder)
                current_dir = idx;
            else
                print("Folder not found\n\n");
        }
    }

    // prnline <text>
    else if (input_length > 8 && input_buffer[0]=='p' && input_buffer[1]=='r' && input_buffer[2]=='n') {
        print(&input_buffer[8]);
        print("\n\n");
    }

    // write <file>
    else if (input_length > 6 && input_buffer[0]=='w' && input_buffer[1]=='r' && input_buffer[2]=='i') {
        clear_screen();
        notepad(&input_buffer[6]);
    }

    else if (input_length > 0)
        print("Unknown command\n\n");

    print_prompt();
}

/* ================= KERNEL MAIN ================= */

extern "C" void kernel_main()
{
    // Initialize Memory Manager
    uint64_t mem_size = 128 * 1024 * 1024;
    uint64_t bitmap_loc = 0x180000; 
    pmm_init(mem_size, bitmap_loc);

    for (uint64_t addr = 0x200000; addr < mem_size; addr += 4096) {
        pmm_mark_free(addr);
    }
    /* --- PMM TEST BLOCK ---
    void* block1 = pmm_alloc_block();
    void* block2 = pmm_alloc_block();

    if (block1 != nullptr && block2 != nullptr && block1 != block2) 
    {
        print("PMM Status: ONLINE\n");
        print("Allocated Block 1: "); print_hex((uint64_t)block1); putchar('\n');
        print("Allocated Block 2: "); print_hex((uint64_t)block2); putchar('\n');


        // Test the Freeing logic
        pmm_free_block(block1);
        void* block3 = pmm_alloc_block();

        if (block3 == block1) {
            print("PMM Reuse: SUCCESS\n\n");
        } else {
            print("PMM Reuse: FAILED (Unexpected Address)\n\n");
        }
        asm volatile("hlt");
    } 
    else 
    {
        color = 0x4F; // Red Screen
        print("PMM CRITICAL FAILURE: Allocation failed or duplicate addresses!\n");
        while(1) asm("hlt");
    }
    // -----------------------*/

    idt_init();
    pic_remap();
    heap_init();
    /*for(int i = 0; i < 1000; i++) {
    uint8_t* test = new uint8_t[1024]; // Allocate 1KB
    delete[] test;                    // Free 1KB
}
print("Heap Stress Test: PASSED (No leakage)\n");
asm volatile("hlt");*/
    
    extern void pic_clear_mask(uint8_t irq);
    pic_clear_mask(1); // Enable Keyboard
    // test_new_operator();
    
    asm volatile ("sti");

    enable_cursor(0, 15);
    fs_init();
    splash_screen();
    print_prompt();

    while (true)
    {
        char c = keyboard_getchar();
        if (!c) continue;

        if (c == '\b') {
            if (input_length > 0 && col > prompt_start_col) {
                input_length--;
                col--;
                vga[row * VGA_WIDTH + col] = (color << 8) | ' ';
                update_cursor();
            }
        }
        else if (c == '\n')
            handle_enter();
        else if (input_length < 255) {
            input_buffer[input_length++] = c;
            putchar(c);
        }
    }
}

/* ================ INTERRUPTS ================== */

extern "C" void isr_dispatch(interrupt_frame* frame)
{
    color = 0x4F; 
    clear_screen();

    print("==== KERNEL PANIC ====\n\n");
    print("Interrupt: "); print_hex(frame->int_no); putchar('\n');
    print("Error:     "); print_hex(frame->err_code); putchar('\n');
    print("RIP:       "); print_hex(frame->rip); putchar('\n');

    if (frame->int_no == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        print("Fault Addr: "); print_hex(cr2); putchar('\n');
    }

    while (1) asm("hlt");
}

extern "C" void isr_timer()
{
    timer_ticks++;
    pic_send_eoi(0);
}