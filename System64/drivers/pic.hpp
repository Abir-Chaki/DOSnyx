#pragma once
#include <stdint.h>

void pic_remap();
void pic_send_eoi(unsigned char irq);
void pic_clear_mask(uint8_t irq);