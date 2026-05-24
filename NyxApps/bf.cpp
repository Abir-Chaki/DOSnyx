#include <stdint.h>
#include "../System64/fs.hpp"
#include "../System64/drivers/keyboard.hpp"

extern "C" {
    void putchar(char c);
    void print(const char* str);
    //char keyboard_getchar();
}

void bf_interpret(const char* code) {
    uint8_t tape[3000] = {0}; // Reduced size for kernel stack safety
    uint8_t* ptr = tape;
    
    for (int i = 0; code[i] != '\0'; i++) {
        switch (code[i]) {
            case '>': ptr++; break;
            case '<': ptr--; break;
            case '+': (*ptr)++; break;
            case '-': (*ptr)--; break;
            case '.': putchar((char)*ptr); break;
            case ',': *ptr = (uint8_t)keyboard_getchar(); break;
            case '[':
                if (*ptr == 0) {
                    int depth = 1;
                    while (depth > 0) {
                        i++;
                        if (code[i] == '[') depth++;
                        if (code[i] == ']') depth--;
                    }
                }
                break;
            case ']':
                if (*ptr != 0) {
                    int depth = 1;
                    while (depth > 0) {
                        i--;
                        if (code[i] == '[') depth--;
                        if (code[i] == ']') depth++;
                    }
                }
                break;
        }
    }
}

extern "C" void run_bf(Node* file_node) {
    if (!file_node) return;
    bf_interpret(file_node->content);
}