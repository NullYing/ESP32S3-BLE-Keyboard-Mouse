#include "usb_keyboard_report.h"
#include <limits.h>
#include <string.h>

#define MAX_USAGES 32
#define MAX_STACK 4

typedef struct {
  uint16_t page;
  uint8_t id;
  uint32_t size, count;
  int32_t logical_min;
} globals_t;
typedef struct { uint32_t min, max; } usage_range_t;

static uint32_t item_value(const uint8_t *data, size_t size) {
  uint32_t value = 0;
  for (size_t i = 0; i < size; i++) value |= (uint32_t)data[i] << (8 * i);
  return value;
}

static int report_index(usb_keyboard_decoder_t *d, uint8_t id) {
  for (int i = 0; i < d->report_count; i++) if (d->reports[i].id == id) return i;
  if (d->report_count == USB_KEYBOARD_MAX_REPORTS) return -1;
  int index = d->report_count++;
  d->reports[index].id = id;
  return index;
}

static bool add_field(usb_keyboard_decoder_t *d, int report, const globals_t *g,
                       usage_range_t range, uint32_t offset, uint32_t count,
                       bool variable) {
  uint16_t page = range.min >> 16;
  if (!page) page = g->page;
  if (page != 7) return true;
  if ((range.max >> 16) != (range.min >> 16) || (uint16_t)range.max < (uint16_t)range.min ||
      g->size == 0 || g->size > 32 || count > UINT16_MAX ||
      d->field_count == USB_KEYBOARD_MAX_FIELDS) return false;
  d->fields[d->field_count++] = (usb_keyboard_field_t){
    .bit_offset = offset, .usage_min = (uint16_t)range.min,
    .usage_max = (uint16_t)range.max, .usage_page = page,
    .logical_min = g->logical_min, .count = count, .size = g->size,
    .report_index = report, .variable = variable};
  d->reports[report].keyboard = true;
  return true;
}

bool usb_keyboard_decoder_init(usb_keyboard_decoder_t *d, const uint8_t *desc, size_t length) {
  if (!d) return false;
  memset(d, 0, sizeof(*d));
  if (!desc || !length) return false;
  globals_t g = {0}, stack[MAX_STACK];
  usage_range_t usages[MAX_USAGES];
  int usage_count = 0, stack_count = 0, depth = 0;
  bool pending_min = false;
  for (size_t cursor = 0; cursor < length;) {
    uint8_t prefix = desc[cursor++];
    if (prefix == 0xfe) {
      if (length - cursor < 2 || desc[cursor] > length - cursor - 2) return false;
      cursor += 2 + desc[cursor];
      continue;
    }
    size_t size = prefix & 3;
    if (size == 3) size = 4;
    if (size > length - cursor) return false;
    uint32_t value = item_value(desc + cursor, size);
    cursor += size;
    uint8_t type = prefix & 0x0c, tag = prefix & 0xf0;
    if (type == 4) {
      switch (tag) {
      case 0x00: if (value > UINT16_MAX) return false; g.page = value; break;
      case 0x10:
        if (size == 1) g.logical_min = (int8_t)value;
        else if (size == 2) g.logical_min = (int16_t)value;
        else g.logical_min = (int32_t)value;
        break;
      case 0x70: g.size = value; break;
      case 0x80:
        if (!value || value > UINT8_MAX) return false;
        g.id = value; d->has_report_ids = true; break;
      case 0x90: g.count = value; break;
      case 0xa0: if (stack_count == MAX_STACK) return false; stack[stack_count++] = g; break;
      case 0xb0: if (!stack_count) return false; g = stack[--stack_count]; break;
      }
    } else if (type == 8) {
      if (tag == 0x00 || tag == 0x10) {
        if (usage_count == MAX_USAGES || pending_min) return false;
        usages[usage_count++] = (usage_range_t){value, value};
        pending_min = tag == 0x10;
      } else if (tag == 0x20) {
        if (!pending_min || value < usages[usage_count - 1].min) return false;
        usages[usage_count - 1].max = value;
        pending_min = false;
      }
    } else if (type == 0) {
      if (pending_min) return false;
      if (tag == 0xa0) depth++;
      else if (tag == 0xc0) { if (!depth) return false; depth--; }
      else if (tag == 0x80) {
        if (g.size > 32 || g.count > UINT16_MAX || (g.count && !g.size)) return false;
        int index = report_index(d, g.id);
        if (index < 0) return false;
        uint32_t offset = d->reports[index].input_bits;
        uint32_t bits = g.size * g.count;
        if (offset > UINT32_MAX - bits) return false;
        if (!(value & 1)) {
          bool variable = (value & 2) != 0;
          uint32_t consumed = 0;
          int64_t selector_min = g.logical_min;
          for (int i = 0; i < usage_count; i++) {
            if (variable && consumed >= g.count) break;
            uint32_t range_count = (uint16_t)usages[i].max - (uint16_t)usages[i].min + 1;
            if (variable) {
              uint32_t count = range_count;
              if (count > g.count - consumed) count = g.count - consumed;
              if (!add_field(d, index, &g, usages[i], offset + consumed * g.size, count, true)) return false;
              consumed += count;
            } else {
              globals_t array_globals = g;
              if (selector_min < INT32_MIN || selector_min > INT32_MAX) return false;
              array_globals.logical_min = (int32_t)selector_min;
              if (!add_field(d, index, &array_globals, usages[i], offset, g.count, false)) return false;
              selector_min += range_count;
            }
          }
        }
        d->reports[index].input_bits += bits;
      }
      usage_count = 0; pending_min = false;
    }
  }
  if (depth || stack_count || pending_min || !d->field_count) return false;
  // Numbered and unnumbered Input reports cannot coexist.
  if (d->has_report_ids)
    for (int i = 0; i < d->report_count; i++)
      if (!d->reports[i].id && d->reports[i].input_bits) return false;
  return true;
}

static uint32_t get_bits(const uint8_t *data, uint32_t offset, unsigned size) {
  uint32_t value = 0;
  for (unsigned i = 0; i < size; i++)
    value |= (uint32_t)((data[(offset + i) / 8] >> ((offset + i) % 8)) & 1) << i;
  return value;
}

bool usb_keyboard_decode(usb_keyboard_decoder_t *d, const uint8_t *data,
                         size_t length, uint8_t out[8]) {
  if (!d || !data || !length || !out) return false;
  uint8_t id = d->has_report_ids ? data[0] : 0;
  size_t prefix = d->has_report_ids ? 1 : 0;
  int index = -1;
  for (int i = 0; i < d->report_count; i++) if (d->reports[i].id == id) { index = i; break; }
  if (index < 0 || !d->reports[index].keyboard ||
      (length - prefix) * 8 < d->reports[index].input_bits) return false;
  uint8_t pressed[32] = {0};
  for (int i = 0; i < d->field_count; i++) {
    const usb_keyboard_field_t *f = &d->fields[i];
    if (f->report_index != index) continue;
    for (uint32_t k = 0; k < f->count; k++) {
      uint32_t value = get_bits(data + prefix, f->bit_offset + k * f->size, f->size);
      int64_t usage = f->variable ? (int64_t)f->usage_min + k : (int64_t)value - f->logical_min + f->usage_min;
      if (f->variable && !value) continue;
      if (usage <= 0 || usage < f->usage_min || usage > f->usage_max || usage > 255) continue;
      pressed[usage / 8] |= 1U << (usage % 8);
    }
  }
  memcpy(d->reports[index].pressed, pressed, sizeof(pressed));
  memset(out, 0, 8);
  unsigned keys = 0;
  bool rollover = false;
  for (unsigned usage = 1; usage < 256; usage++) {
    bool down = false;
    for (int i = 0; i < d->report_count; i++) down |= (d->reports[i].pressed[usage / 8] >> (usage % 8)) & 1;
    if (!down) continue;
    if (usage >= 0xe0 && usage <= 0xe7) out[0] |= 1U << (usage - 0xe0);
    else if (usage <= 3) rollover = true;
    else if (keys < 6) out[2 + keys++] = usage;
    else rollover = true;
  }
  if (rollover) memset(out + 2, 1, 6);
  return true;
}
