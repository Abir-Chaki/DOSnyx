#include "tui.hpp"

extern volatile uint16_t* vga;

void draw_tui_window(TUIWindow* win) {
    if (!win->visible) return;

    // Draw Corners using your CP437 double-lines
    vga[win->y * 80 + win->x] = (win->border_color << 8) | 0xC9;
    vga[win->y * 80 + (win->x + win->w - 1)] = (win->border_color << 8) | 0xBB;
    vga[(win->y + win->h - 1) * 80 + win->x] = (win->border_color << 8) | 0xC8;
    vga[(win->y + win->h - 1) * 80 + (win->x + win->w - 1)] = (win->border_color << 8) | 0xBC;

    // Draw horizontal and vertical bounds
    for (int i = 1; i < win->w - 1; i++) {
        vga[win->y * 80 + (win->x + i)] = (win->border_color << 8) | 0xCD;
        vga[(win->y + win->h - 1) * 80 + (win->x + i)] = (win->border_color << 8) | 0xCD;
    }
    for (int i = 1; i < win->h - 1; i++) {
        vga[(win->y + i) * 80 + win->x] = (win->border_color << 8) | 0xBA;
        vga[(win->y + i) * 80 + (win->x + win->w - 1)] = (win->border_color << 8) | 0xBA;
        
        for(int j = 1; j < win->w - 1; j++) {
            vga[(win->y + i) * 80 + (win->x + j)] = (win->border_color << 8) | ' ';
        }
    }

    int len = 0; while(win->title[len]) len++;
    int tx = win->x + (win->w / 2) - (len / 2);
    for(int i = 0; i < len; i++) vga[win->y * 80 + (tx + i)] = (win->border_color << 8) | win->title[i];
}

void draw_taskbar() {
    // Row 0: Dark Blue Title bar with centered text
    for (int i = 0; i < 80; i++) vga[i] = (0x1F << 8) | ' '; 
    
    const char* title = "DOSnyx Operating System v3.5";
    int len = 0; while(title[len]) len++;
    int start_x = (80 - len) / 2;
    for (int i = 0; i < len; i++) vga[start_x + i] = (0x1F << 8) | title[i];
    
    // Maintain your clickable red [INFO] button on the far right
    const char* btn = "[INFO]";
    for(int i = 0; btn[i]; i++) vga[73 + i] = (0x4F << 8) | btn[i];
}

void draw_v3_menu(int current_selection) {
    // Clear the menu rows (Line 1 to 4) back to an empty dark canvas
    for (int y = 1; y <= 4; y++) {
        for (int x = 0; x < 80; x++) vga[y * 80 + x] = (0x0F << 8) | ' ';
    }

    // Color states: Highlight = White on Cyan (0x3F) | Sleeping = Standard White (0x0F)
    uint8_t m0 = (current_selection == 0) ? 0x3F : 0x0F;
    uint8_t m1 = (current_selection == 1) ? 0x3F : 0x0F;
    uint8_t m2 = (current_selection == 2) ? 0x3F : 0x0F;
    uint8_t m3 = (current_selection == 3) ? 0x3F : 0x0F;
    uint8_t m4 = (current_selection == 4) ? 0x3F : 0x0F;

    // Left column: Applications
    const char* s0 = " [1] Command Prompt ";
    const char* s1 = " [2] File Manager   ";
    const char* s2 = " [3] Text Editor    ";

    // Right column: Power Controls
    const char* s3 = " [4] Restart System ";
    const char* s4 = " [5] Shutdown Nyx   ";

    // Render applications column
    for(int i = 0; s0[i]; i++) vga[1 * 80 + 4 + i] = (m0 << 8) | s0[i];
    for(int i = 0; s1[i]; i++) vga[2 * 80 + 4 + i] = (m1 << 8) | s1[i];
    for(int i = 0; s2[i]; i++) vga[3 * 80 + 4 + i] = (m2 << 8) | s2[i];

    // Render power controls column (starting at Column 45)
    for(int i = 0; s3[i]; i++) vga[1 * 80 + 45 + i] = (m3 << 8) | s3[i];
    for(int i = 0; s4[i]; i++) vga[2 * 80 + 45 + i] = (m4 << 8) | s4[i];

    // Build the divider line at row 5
    for (int x = 0; x < 80; x++) vga[5 * 80 + x] = (0x07 << 8) | '-';
}