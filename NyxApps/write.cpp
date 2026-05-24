#include <stdint.h>
#include "write.hpp"
#include "../System64/fs.hpp"
#include "../System64/drivers/keyboard.hpp"

extern "C" {
    void putchar(char c);
    void print(const char* str);
    void clear_workspace(); // Linked to our workspace sandbox
}

extern int col;
extern int row;
extern uint8_t color;
extern volatile uint16_t* vga;

static const int VGA_WIDTH = 80;

void notepad(Node* file_node)
{
    if (!file_node) return;

    clear_workspace();

    print("--- DOSnyx Notepad v3.0 ---\n");
    print("Editing target segment: "); print(file_node->name);
    print("\n[ESC]: Save and Return to Main Panel | [BACKSPACE]: Delete\n");
    print("----------------------------------------------------------\n\n");

    int len = file_node->size;

    if (len > 0) {
        for (int i = 0; i < len; i++) putchar(file_node->content[i]);
    }

    while (true)
    {
        char c = keyboard_getchar();
        if (!c) continue;

        if (c == 27) { // ESC exits and saves back to the node array
            file_node->size = len;
            if (len < 1023) file_node->content[len] = '\0'; 
            return;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                if (col > 0) {
                    col--;
                } else if (row > 6) { // Block backspace cursor from breaking above Row 6 line split
                    row--;
                    col = VGA_WIDTH - 1;
                }
                vga[row * VGA_WIDTH + col] = (color << 8) | ' ';
            }
            continue;
        }

        if (c == '\t') {
            for (int i = 0; i < 4; i++) {
                if (len < 1023) {
                    file_node->content[len++] = ' ';
                    putchar(' ');
                }
            }
            continue;
        }

        if (len < 1023) {
            file_node->content[len++] = c;
            putchar(c);
        }
    }
}