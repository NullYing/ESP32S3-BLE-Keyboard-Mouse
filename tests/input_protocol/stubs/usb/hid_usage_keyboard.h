#pragma once
#include <stdint.h>
#define HID_KEYBOARD_KEY_MAX 6
typedef struct { struct { uint8_t val; } modifier; uint8_t reserved; uint8_t key[6]; } hid_keyboard_input_report_boot_t;
