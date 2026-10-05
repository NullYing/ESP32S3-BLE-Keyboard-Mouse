#!/usr/bin/env python3
"""Compile real application USB callbacks against host stubs, without editing source."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/hid_host_example.c").read_text()
# Strip lexical braces in comments/literals while preserving offsets for extraction.
masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda match: " " * len(match.group()), source, flags=re.S)

def extract(name):
    match = re.search(r"(?:static\s+)?(?:int32_t|void)\s+" + name + r"\s*\([^;]*?\)\s*\{", masked)
    if not match:
        raise RuntimeError(f"Could not locate production function {name}")
    depth, end = 1, match.end()
    while depth:
        if masked[end] == "{": depth += 1
        if masked[end] == "}": depth -= 1
        end += 1
    return source[match.start():end]

names = ["clamp_axis", "hid_host_mouse_report_callback", "request_recovery",
         "defer_interface_close", "close_disconnected_interfaces",
         "recover_pending_interfaces", "usb_hid_host_interface_callback",
         "print_usb_device_info", "usb_hid_host_device_event"]
globals_start = source.index("static usb_hid_devices_t usb_hid_devices")
globals_end = source.index("// LED控制", globals_start)
prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "hid_report_parser_c.h"
#include "keyboard_handler.h"
#include "ble_hid_send.h"
#include "hid_config.h"
#include "usb_hid_types.h"
#include "hid_device_type_detector.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
static const char *TAG_HID = "HID", *TAG_USB = "USB";
static QueueHandle_t app_event_queue = (void *)2;
void update_led_color(void);
void mouse_accumulator_add(int16_t, int16_t, int8_t, uint8_t);
void mouse_accumulator_clear(void);
void usb_hid_recovery_poll(void);
esp_err_t usb_hid_recover(hid_host_device_handle_t);
int xQueueSend(QueueHandle_t, const void *, unsigned);
'''
with tempfile.TemporaryDirectory() as workdir:
    unit = Path(workdir) / "usb_integration.c"
    unit.write_text(prelude + source[globals_start:globals_end] + "\n" +
                    "\n".join(extract(name) for name in names) + "\n" +
                    (ROOT / "tests/usb_integration/fixture.c").read_text())
    binary = Path(workdir) / "usb_integration"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "tests/usb_integration/stubs"),
                    "-I" + str(ROOT / "tests/input_protocol/stubs"), "-I" + str(ROOT / "main"),
                    str(unit), str(ROOT / "main/hid_report_parser.c"),
                    str(ROOT / "main/usb_keyboard_report.c"), str(ROOT / "main/keyboard_handler.c"),
                    str(ROOT / "main/hid_device_type_detector.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
