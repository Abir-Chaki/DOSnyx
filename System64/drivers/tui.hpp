#ifndef TUI_HPP
#define TUI_HPP

#include <stdint.h>

struct TUIWindow {
    int x, y, w, h;
    const char* title;
    uint8_t border_color;
    bool visible;
};

extern volatile uint16_t* vga;

void draw_tui_window(TUIWindow* win);
void draw_taskbar();
void draw_v3_menu(int current_selection); // <-- Added for Version 3 Selection

#endif