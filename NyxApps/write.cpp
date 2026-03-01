#include <stdint.h>
#include "../System64/fs.hpp"
#include "../System64/drivers/keyboard.hpp"

extern "C" {
    void putchar(char c);
    void print(const char* str);
}

extern int col;
extern int row;
extern uint8_t color;
extern volatile uint16_t* vga;

static const int VGA_WIDTH = 80;

/* ================= INTERNAL HELPERS ================= */

/*int fs_find_local(const char* name)
{
    for (int i = 0; i < MAX_NODES; i++)
        if (nodes[i].used &&
            nodes[i].parent == current_dir &&
            strcmp_simple(nodes[i].name, name))
            return i;
    return -1;
}*/

/* ================= NOTEPAD ================= */

void notepad(const char* filename)
{
    int file_idx = fs_find(filename);

    // If file does not exist → create
    if (file_idx < 0)
        file_idx = fs_create(filename, false);

    print("\n--- DOSnyx Notepad ---\n");
    print("File: ");
    print(filename);
    print("\nESC to save & exit\n\n");

    char buffer[1024];
    int len = 0;

    // LOAD EXISTING CONTENT
    if (nodes[file_idx].size > 0)
    {
        for (int i = 0; i < nodes[file_idx].size; i++)
        {
            buffer[i] = nodes[file_idx].content[i];
            putchar(buffer[i]);
        }
        len = nodes[file_idx].size;
    }

    while (true)
    {
        char c = keyboard_getchar();
        if (!c) continue;

        // ESC → SAVE & EXIT
        if (c == 27)
        {
            for (int i = 0; i < len; i++)
                nodes[file_idx].content[i] = buffer[i];

            nodes[file_idx].size = len;

            print("\n\nSaved.\n");
            return;
        }

        // BACKSPACE
        if (c == '\b')
        {
            if (len > 0)
            {
                len--;
                if (col > 0)
                    col--;

                vga[row * VGA_WIDTH + col] = (color << 8) | ' ';
            }
            continue;
        }

        // TAB → 4 spaces
        if (c == '\t')
        {
            for (int i = 0; i < 4; i++)
            {
                if (len < 1023)
                {
                    buffer[len++] = ' ';
                    putchar(' ');
                }
            }
            continue;
        }

        // NORMAL CHARACTER
        if (len < 1023)
        {
            buffer[len++] = c;
            putchar(c);
        }
    }
}