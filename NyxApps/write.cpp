#include <stdint.h>
#include "../System64/fs.hpp"
#include "../System64/drivers/keyboard.hpp"

extern "C" {
    void putchar(char c);
    void print(const char* str);
    void clear_screen();
}

extern int col;
extern int row;
extern uint8_t color;
extern volatile uint16_t* vga;

static const int VGA_WIDTH = 80;

/* ================= NOTEPAD ================= */

void notepad(Node* file_node)
{
    // Safety check: if the pointer is null, get out immediately
    if (!file_node) return;

    print("\n--- DOSnyx Notepad v2.6 ---\n");
    print("Editing: ");
    print(file_node->name);
    print("\n[ESC]: Save & Exit | [BACKSPACE]: Delete\n");
    print("------------------------------------------\n\n");

    // We use the Node's existing size to track where we are in the buffer
    int len = file_node->size;

    // LOAD EXISTING CONTENT FROM THE NODE
    if (len > 0)
    {
        for (int i = 0; i < len; i++)
        {
            putchar(file_node->content[i]);
        }
    }

    while (true)
    {
        char c = keyboard_getchar();
        if (!c) continue;

        // ESC (ASCII 27) -> SAVE & EXIT
        if (c == 27)
        {
            file_node->size = len;
            // Ensure the content is null-terminated for safety
            if (len < 1023) file_node->content[len] = '\0'; 
            
            print("\n\nChanges saved to heap node.\n");
            return;
        }

        // BACKSPACE
        if (c == '\b')
        {
            if (len > 0)
            {
                len--;
                if (col > 0) {
                    col--;
                } else if (row > 0) {
                    // Simple wrap-back logic if needed
                    row--;
                    col = VGA_WIDTH - 1;
                }
                vga[row * VGA_WIDTH + col] = (color << 8) | ' ';
            }
            continue;
        }

        // TAB -> 4 spaces
        if (c == '\t')
        {
            for (int i = 0; i < 4; i++)
            {
                if (len < 1023) // Still respecting the Node's content array limit
                {
                    file_node->content[len++] = ' ';
                    putchar(' ');
                }
            }
            continue;
        }

        // NORMAL CHARACTER
        // Note: Currently limited to 1024 bytes per Node definition.
        // In v2.7, we can implement realloc() to make this infinite!
        if (len < 1023)
        {
            file_node->content[len++] = c;
            putchar(c);
        }
    }
}