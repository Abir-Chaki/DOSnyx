#include "keyboard.hpp"
#include "pic.hpp"

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define KB_BUF_SIZE 64

static volatile char kb_buffer[KB_BUF_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;
static bool shift_pressed = false;

static const char normal_table[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0
};

static const char shift_table[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0
};

static bool extended = false;

extern "C" void isr_keyboard()
{
    uint8_t sc = inb(0x60);

    // Handle extended prefix
    if (sc == 0xE0)
    {
        extended = true;
        pic_send_eoi(1);
        return;
    }

    // Ignore extended keys for now
    if (extended)
    {
        extended = false;
        pic_send_eoi(1);
        return;
    }

    // Shift press
    if (sc == 0x2A || sc == 0x36)
    {
        shift_pressed = true;
        pic_send_eoi(1);
        return;
    }

    // Shift release
    if (sc == 0xAA || sc == 0xB6)
    {
        shift_pressed = false;
        pic_send_eoi(1);
        return;
    }

    // Ignore key release events
    if (sc & 0x80)
    {
        pic_send_eoi(1);
        return;
    }

    // Safety bounds check
    if (sc >= 128)
    {
        pic_send_eoi(1);
        return;
    }

    char c = shift_pressed ? shift_table[sc] : normal_table[sc];

    if (c)
    {
        int next = (kb_head + 1) % KB_BUF_SIZE;
        if (next != kb_tail)
        {
            kb_buffer[kb_head] = c;
            kb_head = next;
        }
    }

    pic_send_eoi(1);
}

void keyboard_init()
{
    // nothing for now
}

char keyboard_getchar()
{
    if (kb_head == kb_tail)
        return 0;

    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}