#include <stdint.h>
#include "fs.hpp"
#include "memory/heap.hpp"
#include "memory/pmm.hpp"
#include "drivers/idt.hpp"
#include "drivers/pic.hpp"
#include "drivers/keyboard.hpp"
#include "drivers/tui.hpp"

struct interrupt_frame {
    uint64_t r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code, rip, cs, rflags, rsp, ss;
}; 

extern void notepad(Node* file_node); 
extern "C" void run_bf(Node* file_node);
extern "C" void kernel_main(); 

/* ================= VGA & UI GLOBALS ================= */
static const int VGA_WIDTH  = 80; 
static const int VGA_HEIGHT = 25; 
volatile uint16_t* vga = (uint16_t*)0xB8000; 
extern "C" volatile uint64_t timer_ticks = 0; 

uint8_t color = 0x0F; 
int row = 6; // Starts at row 6 to sit beneath the new V3 System Menu
int col = 0; 
char input_buffer[256]; 
int input_length = 0; 
int prompt_start_col = 0; 
char username[32] = "nyx authority/dos"; 

TUIWindow sys_info_win = { 20, 7, 40, 8, " SYSTEM STATUS ", 0x1E, false };

/* ================= V3 EXECUTION STATE GLOBALS ================= */
enum ShellMode { MODE_MENU_NAV, MODE_APP_ACTIVE };
static ShellMode current_mode = MODE_MENU_NAV;
static int active_selection = 0;

/* ================= FILE SYSTEM GLOBALS ================= */
Node* all_nodes_head = nullptr; 
Node* current_dir_ptr = nullptr; 

/* ================= MOUSE GLOBALS ================= */
static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];
static int mouse_x = 40, mouse_y = 12;
static uint16_t mouse_back[1][1]; 
static const int MOUSE_SENSITIVITY = 4;

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
extern "C" void* memset(void* dest, int val, size_t len) {
    unsigned char* ptr = (unsigned char*)dest;
    while (len-- > 0) *ptr++ = (unsigned char)val;
    return dest;
}

/* ================= SCREEN CONTROL ================= */

void update_cursor() {
    uint16_t pos = row * VGA_WIDTH + col;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF)); 
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF)); 
}

void scroll() {
    // Shifts text up, bounded between Row 6 and Row 24
    for (int i = 6; i < VGA_HEIGHT - 1; i++) {
        for (int j = 0; j < VGA_WIDTH; j++)
            vga[i * VGA_WIDTH + j] = vga[(i + 1) * VGA_WIDTH + j];
    }
    for (int i = 0; i < VGA_WIDTH; i++) vga[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = (color << 8) | ' ';
    row = VGA_HEIGHT - 1; 
}

extern "C" void clear_workspace() {
    // Clears workspace panel safely beneath row 5
    for (int i = 6 * VGA_WIDTH; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = (color << 8) | ' '; 
    row = 6; col = 0;
    update_cursor(); 
}

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = (color << 8) | ' '; 
    row = 1; col = 0;
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

void enable_cursor(uint8_t start, uint8_t end) {
    outb(0x3D4, 0x0A); 
    outb(0x3D5, (inb(0x3D5) & 0xC0) | start); 
    outb(0x3D4, 0x0B); 
    outb(0x3D5, (inb(0x3D5) & 0xE0) | end); 
}

void splash_screen() {
    color = 0x1F; clear_screen(); 
    print("DOSnyx Operating System\n"); 
    print("Version 3.0 (TUI Engine)\n\n"); 
    print("Press any key to boot the system desktop..."); 
    while (!keyboard_getchar()); 
    color = 0x0F;
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
    Node* root = new Node(); 
    if (root == nullptr) {
        color = 0x1F; print("CRITICAL: Heap returned NULL for Root Node!\n"); 
        while(1) asm("hlt"); 
    }
    print("Address: "); print_hex((uint64_t)root); putchar('\n'); 
    kstrncpy(root->name, "root", 15);
    root->is_folder = true; root->parent = nullptr; 
    root->next = nullptr; root->size = 0; 
    all_nodes_head = root; current_dir_ptr = root; 
}

Node* fs_find(const char* name) {
    Node* curr = all_nodes_head;
    while (curr != nullptr) {
        if (curr->parent == current_dir_ptr && strcmp_simple(curr->name, name)) return curr; 
        curr = curr->next; 
    }
    return nullptr;
}

Node* fs_create(const char* name, bool folder) {
    Node* newNode = new Node(); 
    kstrncpy(newNode->name, name, 15);
    newNode->is_folder = folder; newNode->parent = current_dir_ptr; 
    newNode->size = 0; newNode->content[0] = 0; 
    newNode->next = all_nodes_head; 
    all_nodes_head = newNode;
    return newNode; 
}

void fs_delete(const char* name) {
    Node* curr = all_nodes_head; Node* prev = nullptr;
    while (curr != nullptr) {
        if (curr->parent == current_dir_ptr && strcmp_simple(curr->name, name)) {
            if (prev == nullptr) all_nodes_head = curr->next; 
            else prev->next = curr->next; 
            delete curr; return; 
        }
        prev = curr; curr = curr->next; 
    }
}

/* ================= SHELL UI ================= */

void print_path(Node* dir) {
    if (dir == nullptr || dir->parent == nullptr) return;
    print_path(dir->parent); print(dir->name); print("/");
}

void print_prompt() {
    if (col != 0) putchar('\n');
    print("1:/"); print_path(current_dir_ptr); print("> ");
    prompt_start_col = col; input_length = 0;
}

void show_cmds() {
    print("ver - Version Info\nabout - About DOSnyx\ncl - Clear window workspace\nrst - Reboot\n");
    print("dir - List files\ncreate <f> - New file\ndel <f> - Delete\nmf <f> - New folder\n");
    print("cf <f> - Change folder\nusrnm - Current user\nwrite <f> - Notepad\nbf <f> - Brainf**k Interpreter\n");
}

void handle_enter() {
    input_buffer[input_length] = 0; putchar('\n');
    if (strcmp_simple(input_buffer, "ver")) print("DOSnyx Version 3.0\n\n");
    else if (strcmp_simple(input_buffer, "about")) print("DOSnyx: Hybrid Kernel Concept\nBy Abir Chaki | 13 y/o Dev | India\n\n");
    else if (strcmp_simple(input_buffer, "cl")) { clear_workspace(); print_prompt(); return; } 
    else if (strcmp_simple(input_buffer, "rst")) hard_reboot();
    else if (strcmp_simple(input_buffer, "usrnm")) { print(username); print("\n\n"); } 
    else if (strcmp_simple(input_buffer, "cmds")) show_cmds();
    else if (input_length > 3 && input_buffer[0]=='b' && input_buffer[1]=='f') {
        Node* target = fs_find(&input_buffer[3]);
        if (target && !target->is_folder) {
            run_bf(target);
        } else {
            print("BF file not found.\n");
        }
    }
    else if (strcmp_simple(input_buffer, "dir")) {
        Node* curr = all_nodes_head;
        while (curr != nullptr) {
            if (curr->parent == current_dir_ptr) {
                print(curr->is_folder ? "[DIR]  " : "[FILE] "); print(curr->name); putchar('\n'); 
            }
            curr = curr->next; 
        }
        putchar('\n');
    }
    else if (input_length > 7 && input_buffer[0]=='c' && input_buffer[1]=='r') { fs_create(&input_buffer[7], false); print("File created.\n\n"); } 
    else if (input_length > 4 && input_buffer[0]=='d' && input_buffer[1]=='e') { fs_delete(&input_buffer[4]); print("Deleted.\n\n"); } 
    else if (input_length > 3 && input_buffer[0]=='m' && input_buffer[1]=='f') { fs_create(&input_buffer[3], true); print("Folder created.\n\n"); } 
    else if (input_length > 3 && input_buffer[0]=='c' && input_buffer[1]=='f') {
        if (input_buffer[3]=='.' && input_buffer[4]=='.') { if (current_dir_ptr->parent != nullptr) current_dir_ptr = current_dir_ptr->parent; }
        else { Node* target = fs_find(&input_buffer[3]); if (target && target->is_folder) current_dir_ptr = target; else print("Folder not found.\n\n"); } 
    }
    else if (input_length > 8 && input_buffer[0]=='p' && input_buffer[1]=='r' && input_buffer[2]=='n') { print(&input_buffer[8]); print("\n\n"); } 
    else if (input_length > 6 && input_buffer[0]=='w' && input_buffer[1]=='r') {
        Node* target = fs_find(&input_buffer[6]); if (!target) target = fs_create(&input_buffer[6], false);
        notepad(target); 
    }
    else if (strcmp_simple(input_buffer, "sht")) {
        clear_workspace(); color = 0x0E; print("Shutting down\n"); 
        for(volatile uint64_t i = 0; i < 900000000; i++) asm volatile("nop"); 
        outw(0xB004, 0x2000); outw(0x604, 0x2000); outw(0x4004, 0x3400); 
        color = 0x1F; print("\nIt is now safe to turn off the computer.\n"); while(1) asm("hlt"); 
    }
    else if (input_length > 0) print("Unknown command\n\n");
    print_prompt(); 
}

/* ================= MOUSE LOGIC ================= */

void clear_mouse() {
    if (mouse_y < 25 && mouse_x < 80)
        vga[mouse_y * 80 + mouse_x] = mouse_back[0][0];
}

void draw_mouse() {
    if (mouse_y < 25 && mouse_x < 80) {
        mouse_back[0][0] = vga[mouse_y * 80 + mouse_x];
        vga[mouse_y * 80 + mouse_x] = (0x0F << 8) | 0x11; 
    }
}

void execute_tui_app() {
    current_mode = MODE_APP_ACTIVE;
    clear_workspace();

    // =========================================================================
    // OPTION 1: [1] COMMAND PROMPT SHELL
    // =========================================================================
    if (active_selection == 0) {
        print("DOSnyx Command Prompt\n");
        print("Type 'cmds' for options. Press 'ESC' to drop back to menu.\n\n");
        print_prompt();
    }
    
    // =========================================================================
    // OPTION 2: [2] FILE MANAGER (WITH MOUSE & DIRECTORY TRAVERSAL)
    // =========================================================================
    else if (active_selection == 1) {
        int selected_file_index = 0;
        
        // Hide the hardware blinking cursor during list navigation
        outb(0x3D4, 0x0A); outb(0x3D5, 0x20);

        bool in_file_manager = true;
        bool force_redraw = true; // High initially to draw the layout on launch

        while (in_file_manager) {
            // Gather all local directory nodes matching current path scope
            Node* local_nodes[64]; 
            int total_files = 0;
            bool has_back_link = (current_dir_ptr->parent != nullptr);
            
            Node* curr = all_nodes_head;
            while (curr != nullptr && total_files < 64) {
                if (curr->parent == current_dir_ptr) {
                    local_nodes[total_files++] = curr;
                }
                curr = curr->next;
            }

            int rendering_bound = total_files + (has_back_link ? 1 : 0);
            if (selected_file_index >= rendering_bound && rendering_bound > 0) {
                selected_file_index = rendering_bound - 1;
            }

            // Only overwrite VGA text mode memory if a redraw is explicitly requested
            if (force_redraw) {
                clear_workspace();
                print("--- File Manager Workspace ---\n");
                print("[TAB]: Cycle Selection | [ENTER / Left Click]: Open | [ESC]: Exit\n");
                print("Current Directory: /"); print_path(current_dir_ptr); print("\n");
                print("------------------------------------------------------------------------\n\n");
                
                int base_row = row; 
                int current_render_idx = 0;

                // Render parent directory link if applicable
                if (has_back_link) {
                    color = (selected_file_index == 0) ? 0x3F : 0x0F; // Cyan highlight if selected
                    print(" [DIR]  ../ [Go Back to Parent]");
                    for (int p = 31; p < 79; p++) putchar(' ');
                    putchar('\n');
                    current_render_idx++;
                }

                // Render folders and files
                for (int i = 0; i < total_files; i++) {
                    color = (selected_file_index == current_render_idx) ? 0x3F : 0x0F;
                    print(local_nodes[i]->is_folder ? " [DIR]  " : " [FILE] ");
                    print(local_nodes[i]->name);

                    int name_len = 0; while(local_nodes[i]->name[name_len]) name_len++;
                    int written = 8 + name_len;
                    for (int p = written; p < 79; p++) putchar(' ');
                    
                    putchar('\n');
                    current_render_idx++;
                }

                color = 0x0F; // Reset to standard layout white
                if (rendering_bound == 0) {
                    print(" (Directory is empty)\n");
                }
                
                force_redraw = false; // Refresh state handled
            }

            // Pull non-blocking keyboard frame input
            char c = keyboard_getchar(); 
            bool trigger_action = false;

            // Interactive Workspace Mouse Hit-Testing
            int base_list_row = 5; // Headers consume rows up to Line 5
            if ((mouse_byte[0] & 0x01) && mouse_y >= base_list_row && mouse_y < (base_list_row + rendering_bound)) {
                int resolved_idx = mouse_y - base_list_row;
                if (resolved_idx >= 0 && resolved_idx < rendering_bound) {
                    if (selected_file_index != resolved_idx) {
                        selected_file_index = resolved_idx;
                        force_redraw = true; // Instantly move the cyan block under the click
                    }
                    trigger_action = true;
                }
            }

            // Input Event Dispatcher
            if (c == 27) { // ESC -> Exit app
                in_file_manager = false;
                break;
            }
            if (c == '\t') { // TAB -> Cycle items
                if (rendering_bound > 0) {
                    selected_file_index = (selected_file_index + 1) % rendering_bound;
                    force_redraw = true;
                }
            }
            if (c == '\n' || trigger_action) { // Action committed
                if (has_back_link && selected_file_index == 0) {
                    current_dir_ptr = current_dir_ptr->parent;
                    selected_file_index = 0;
                    force_redraw = true;
                } else {
                    int target_array_idx = has_back_link ? (selected_file_index - 1) : selected_file_index;
                    Node* target_node = local_nodes[target_array_idx];

                    if (target_node->is_folder) {
                        current_dir_ptr = target_node;
                        selected_file_index = 0;
                        force_redraw = true;
                    } else {
                        enable_cursor(1, 15); // Restore hardware cursor for Notepad typing
                        notepad(target_node);
                        outb(0x3D4, 0x0A); outb(0x3D5, 0x20); // Re-hide cursor on menu return
                        force_redraw = true;
                    }
                }
                
                // CRITICAL DEBOUNCE DRAIN: Wait right here inside the file manager loop 
                // until the user completely lifts their finger off the left mouse button
                while (mouse_byte[0] & 0x01) {
                    asm volatile("nop");
                }
                for(volatile uint64_t d = 0; d < 20000000; d++);
            }

            asm volatile("nop"); // Keep CPU running cleanly without locking the thread
        }

        // Restore normal terminal state when completely exiting the File Manager
        enable_cursor(1, 15);
        current_mode = MODE_MENU_NAV;
        clear_workspace();
        draw_v3_menu(active_selection);
    }
    
    // =========================================================================
    // OPTION 3: [3] TEXT EDITOR PROMPT PHASE
    // =========================================================================
    else if (active_selection == 2) {
        print("Enter workspace target file name: \n> ");
        
        // Initialize structural boundaries so kernel_main can track typing offsets
        prompt_start_col = col; 
        input_length = 0;
        
        // Ensure the hardware blinker is active and tracking the position
        enable_cursor(1, 15);
        update_cursor();
    }
    
    // =========================================================================
    // OPTION 4: [4] RESTART SYSTEM
    // =========================================================================
    else if (active_selection == 3) {
        clear_workspace();
        color = 0x0E; // Yellow status warning
        print("Rebooting system components...\n");
        for(uint64_t i = 0; i < 400000000; i++) { asm volatile("nop"); }
        hard_reboot();
    }
    
    // =========================================================================
    // OPTION 5: [5] SHUTDOWN NYX
    // =========================================================================
    else if (active_selection == 4) {
        clear_workspace();
        color = 0x0C; // Crimson Red shutdown status
        print("Shutting down kernel processing subsystems...\n");
        for(uint64_t i = 0; i < 600000000; i++) { asm volatile("nop"); }
        
        // Trigger ACPI & QEMU/Bochs IO emulator poweroff sequences
        outw(0xB004, 0x2000); 
        outw(0x604, 0x2000); 
        outw(0x4004, 0x3400); 
        
        color = 0x1F; // High visibility Blue Screen warning layout
        print("\nIt is now safe to turn off your computer."); 
        while(1) { asm volatile("hlt"); }
    }
}
extern "C" void isr_mouse() {
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) { pic_send_eoi(12); return; }
    mouse_byte[mouse_cycle++] = inb(0x60);
    
    if (mouse_cycle == 3) {
        mouse_cycle = 0;
        bool left_click = mouse_byte[0] & 0x01;
        
        clear_mouse();
        int x_mov = mouse_byte[1]; int y_mov = mouse_byte[2];
        if (mouse_byte[0] & 0x10) x_mov |= 0xFFFFFF00;
        if (mouse_byte[0] & 0x20) y_mov |= 0xFFFFFF00;
        
        mouse_x += x_mov / MOUSE_SENSITIVITY;
        mouse_y -= y_mov / MOUSE_SENSITIVITY;
        
        if (mouse_x < 0) mouse_x = 0; if (mouse_x > 79) mouse_x = 79;
        if (mouse_y < 0) mouse_y = 0; if (mouse_y > 24) mouse_y = 24;

        if (left_click) {
            // 1. Taskbar [INFO] Button Hit Test
            if (mouse_y == 0 && mouse_x > 72) {
                sys_info_win.visible = !sys_info_win.visible;
                if (sys_info_win.visible) draw_tui_window(&sys_info_win);
                else { clear_workspace(); draw_taskbar(); draw_v3_menu(active_selection); }
            }
            // 2. V3 Clickable Menu Hit-Testing (Only active when not inside an app loop)
            else if (current_mode == MODE_MENU_NAV) {
                // Column 1: Applications (X: 4 to 24)
                if (mouse_x >= 4 && mouse_x <= 24) {
                    if (mouse_y == 1) { active_selection = 0; draw_v3_menu(active_selection); execute_tui_app(); }
                    else if (mouse_y == 2) { active_selection = 1; draw_v3_menu(active_selection); /*execute_tui_app();*/ }
                    else if (mouse_y == 3) { active_selection = 2; draw_v3_menu(active_selection); execute_tui_app(); }
                }
                // Column 2: Power Options (X: 45 to 65)
                else if (mouse_x >= 45 && mouse_x <= 65) {
                    if (mouse_y == 1) { active_selection = 3; draw_v3_menu(active_selection); execute_tui_app(); }
                    else if (mouse_y == 2) { active_selection = 4; draw_v3_menu(active_selection); execute_tui_app(); }
                }
            }
        }
        draw_mouse();
    }
    pic_send_eoi(12);
}

extern "C" void mouse_init() {
    outb(0x64, 0xA8); outb(0x64, 0x20);
    while (!(inb(0x64) & 1));
    uint8_t status = (inb(0x60) | 2);
    outb(0x64, 0x60); outb(0x60, status);
    outb(0x64, 0xD4); outb(0x60, 0xE8); inb(0x60);
    outb(0x64, 0xD4); outb(0x60, 0x00); inb(0x60);
    outb(0x64, 0xD4); outb(0x60, 0xF3); inb(0x60);
    outb(0x64, 0xD4); outb(0x60, 20); inb(0x60);
    outb(0x64, 0xD4); outb(0x60, 0xF4); inb(0x60);
    pic_clear_mask(2); pic_clear_mask(12);
}

/* ================= KERNEL START ================= */

extern "C" void kernel_main() {
    pmm_init(128 * 1024 * 1024, 0x180000); 
    for (uint64_t addr = 0x200000; addr < 128 * 1024 * 1024; addr += 4096) pmm_mark_free(addr); 
    idt_init(); 
    pic_remap(); 
    heap_init(); 
    pic_clear_mask(1); 
    
    mouse_init();
    asm volatile ("sti"); 
    
    fs_init(); 
    enable_cursor(14,15); 
    splash_screen(); 
    
    // Clear screen fully once, then draw structural layout components
    color = 0x0F;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = (color << 8) | ' '; 
    
    draw_taskbar(); 
    draw_v3_menu(active_selection);
    
    row = 6; col = 0;
    update_cursor(); 

    while (true) {
        char c = keyboard_getchar();
        if (current_mode == MODE_MENU_NAV) {
            
            // =========================================================================
            // CASE 1: KEYBOARD NAVIGATION CONFIRMATION
            // =========================================================================
            if (c == '\n') { 
                // Only launch the app if the user explicitly hit ENTER on the selection
                execute_tui_app();
            }
            
            // =========================================================================
            // CASE 2: MOUSE CLICK SELECTION CONFIRMATION
            // =========================================================================
            else if (active_selection == 1 && (mouse_byte[0] & 0x01)) {
                // Wait until the mouse button is physically released in the main thread
                while (mouse_byte[0] & 0x01) {
                    asm volatile("nop");
                }
                
                // Settle delay
                for(volatile uint64_t d = 0; d < 10000000; d++); 
                
                // Launch safely once!
                execute_tui_app();
            }
        }
        if (!c) continue; 

        // GLOBAL ESC CHECK: Snap back to system navigation grid immediately
        if (c == 27) {
            current_mode = MODE_MENU_NAV;
            clear_workspace();
            draw_v3_menu(active_selection);
            continue;
        }

        if (current_mode == MODE_MENU_NAV) {
            if (c == '\t') { // TAB increments through all 5 options now
                active_selection = (active_selection + 1) % 5;
                draw_v3_menu(active_selection);
            }
            else if (c == '1') { active_selection = 0; draw_v3_menu(active_selection); execute_tui_app(); }
            else if (c == '2') { active_selection = 1; draw_v3_menu(active_selection); execute_tui_app(); }
            else if (c == '3') { active_selection = 2; draw_v3_menu(active_selection); execute_tui_app(); }
            else if (c == '4') { active_selection = 3; draw_v3_menu(active_selection); execute_tui_app(); }
            else if (c == '5') { active_selection = 4; draw_v3_menu(active_selection); execute_tui_app(); }
            else if (c == '\n') { execute_tui_app(); }
        }
        else {
            // =================================================================
            // CASE A: TEXT EDITOR FILENAME SETUP PHASE (Option 2)
            // =================================================================
            if (active_selection == 2) {
                if (c == '\n') {
                    input_buffer[input_length] = 0; // Null terminate filename string
                    
                    Node* target = fs_find(input_buffer);
                    if (!target) target = fs_create(input_buffer, false);
                    
                    // Boot into notepad text editing mode
                    notepad(target);
                    
                    // When notepad closes, smoothly snap back to main menu
                    current_mode = MODE_MENU_NAV;
                    clear_workspace();
                    draw_v3_menu(active_selection);
                }
                else if (c == '\b') {
                    if (input_length > 0 && col > prompt_start_col) { 
                        input_length--; col--; 
                        vga[row * VGA_WIDTH + col] = (color << 8) | ' '; 
                        update_cursor(); 
                    }
                }
                else if (input_length < 255 && c >= 32 && c <= 126) { // Only printable characters
                    input_buffer[input_length++] = c; 
                    putchar(c); 
                }
            }
            // =================================================================
            // CASE B: COMMAND LINE INTERACTIVE SHELL (Option 0)
            // =================================================================
            else if (active_selection == 0) {
                if (c == '\n') {
                    handle_enter(); 
                }
                else if (c == '\b') {
                    if (input_length > 0 && col > prompt_start_col) { 
                        input_length--; col--; 
                        vga[row * VGA_WIDTH + col] = (color << 8) | ' '; 
                        update_cursor(); 
                    }
                }
                else if (input_length < 255) { 
                    input_buffer[input_length++] = c; 
                    putchar(c); 
                } 
            }
        }
    }
}

extern "C" void isr_dispatch(interrupt_frame* frame) {
    color = 0x4F; clear_screen(); print("==== KERNEL PANIC ====\n"); 
    print("Int: "); print_hex(frame->int_no); print("\nRIP: "); print_hex(frame->rip); 
    while (1) asm("hlt"); 
}

extern "C" void isr_timer() {
    timer_ticks++; pic_send_eoi(0); 
}