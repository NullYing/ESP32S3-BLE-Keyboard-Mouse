// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText:  2022 Istvan Pasztor
// C language version of HID report parser for mouse layout extraction
#include "hid_report_parser_c.h"
#include <string.h>

// HID item constants
#define ITEM_LONG 0xFE
#define ITEM_TAG_MASK 0xF0
#define ITEM_TYPE_MASK 0x0C
#define ITEM_SIZE_MASK 0x03
#define ITEM_TAG_AND_TYPE_MASK (ITEM_TAG_MASK | ITEM_TYPE_MASK)

#define ITEM_TYPE_MAIN 0x00
#define ITEM_TYPE_GLOBAL 0x04
#define ITEM_TYPE_LOCAL 0x08

// Main items
#define ITEM_INPUT 0x80
#define ITEM_OUTPUT 0x90
#define ITEM_FEATURE 0xB0
#define ITEM_COLLECTION 0xA0
#define ITEM_END_COLLECTION 0xC0

// Global items
#define ITEM_USAGE_PAGE 0x04
#define ITEM_LOGICAL_MIN 0x14
#define ITEM_LOGICAL_MAX 0x24
#define ITEM_REPORT_SIZE 0x74
#define ITEM_REPORT_ID 0x84
#define ITEM_REPORT_COUNT 0x94
#define ITEM_PUSH 0xA4
#define ITEM_POP 0xB4

// Local items
#define ITEM_USAGE 0x08
#define ITEM_USAGE_MIN 0x18
#define ITEM_USAGE_MAX 0x28

// Field flags
#define FLAG_FIELD_VARIABLE 0x02
#define FLAG_FIELD_RELATIVE 0x01

// Local flags
#define FLAG_USAGE_MIN 0x01
#define FLAG_USAGE_MAX 0x02

// Maximum values
#define MAX_USAGE_RANGES 16
#define MAX_PUSH_POP_STACK 4

// Global state for PUSH/POP stack
typedef struct
{
  uint8_t report_id;
  uint16_t usage_page;
  int32_t logical_min;
  int32_t logical_max;
  uint32_t report_size;
  uint32_t report_count;
} global_state_t;

// Usage range structure
typedef struct
{
  uint16_t usage_page;
  uint16_t usage_min;
  uint16_t usage_max;
} usage_range_t;

// Parser state
typedef struct
{
  // Globals
  uint8_t report_id;
  uint16_t usage_page;
  int32_t logical_min;
  int32_t logical_max;
  uint32_t report_size;
  uint32_t report_count;

  // Global stack for PUSH/POP
  global_state_t global_stack[MAX_PUSH_POP_STACK];
  int global_stack_size;

  // Locals
  usage_range_t usage_ranges[MAX_USAGE_RANGES];
  int num_usage_ranges;
  uint8_t flags; // FLAG_USAGE_MIN, FLAG_USAGE_MAX

  // Collection state
  int collection_depth;
  bool in_mouse_collection;

  // Field tracking
  uint32_t current_bit_offset;
  bool first_field_processed;
  bool first_field_has_report_id;

  // Current report layout being built
  hid_report_layout_t *current_layout;
  bool layout_valid;
} parser_state_t;

// Helper functions
static bool usage_data(const uint8_t *p, uint8_t data_size, uint16_t *usage, uint16_t *usage_page)
{
  *usage_page = 0;
  *usage = 0;
  switch (data_size)
  {
  case 4:
    *usage_page = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
    // fall through
  case 2:
    *usage |= (uint16_t)p[1] << 8;
    // fall through
  case 1:
    *usage |= p[0];
    // fall through
  case 0:
    return true;
  default:
    return false;
  }
}

static bool uint8_data(const uint8_t *p, uint8_t data_size, uint8_t *data)
{
  switch (data_size)
  {
  case 4:
    if (p[2] || p[3])
      return false;
    // fall through
  case 2:
    if (p[1])
      return false;
    // fall through
  case 1:
    *data = p[0];
    return true;
  case 0:
    *data = 0;
    return true;
  default:
    return false;
  }
}

static bool uint16_data(const uint8_t *p, uint8_t data_size, uint16_t *data, bool allow_overflow)
{
  switch (data_size)
  {
  case 4:
    if (!allow_overflow && (p[2] | p[3]))
      return false;
    // fall through
  case 2:
    *data = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return true;
  case 1:
    *data = p[0];
    return true;
  case 0:
    *data = 0;
    return true;
  default:
    return false;
  }
}

static bool uint32_data(const uint8_t *p, uint8_t data_size, uint32_t *data)
{
  *data = 0;
  switch (data_size)
  {
  case 4:
    *data |= ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    // fall through
  case 2:
    *data |= (uint32_t)p[1] << 8;
    // fall through
  case 1:
    *data |= p[0];
    // fall through
  case 0:
    return true;
  default:
    return false;
  }
}

static bool int32_data(const uint8_t *p, uint8_t data_size, int32_t *data)
{
  switch (data_size)
  {
  case 4:
    *data = (int32_t)((int32_t)p[0] | ((int32_t)p[1] << 8) |
                      ((int32_t)p[2] << 16) | ((int32_t)p[3] << 24));
    return true;
  case 2:
    *data = (int16_t)((int16_t)p[0] | ((int16_t)p[1] << 8));
    return true;
  case 1:
    *data = (int8_t)p[0];
    return true;
  case 0:
    *data = 0;
    return true;
  default:
    return false;
  }
}

static void reset_parser(parser_state_t *state)
{
  memset(state, 0, sizeof(parser_state_t));
}

static void reset_locals(parser_state_t *state)
{
  state->num_usage_ranges = 0;
  state->flags = 0;
}

// AddUsageRange: exactly like C++ version
// If this range is a continuation of the previously added range
// then extend the previous range instead of adding a new one.
// Note: usage_page can be 0 here, it will be set to global usage_page later in process_input_field
// IMPORTANT: Only extend if the previous range's max + 1 equals the new min AND
// the previous range was not a single-value range (min == max).
// This prevents merging separate USAGE items that happen to be consecutive.
static bool add_usage_range(parser_state_t *state, uint16_t usage_min, uint16_t usage_max, uint16_t usage_page)
{
  // Try to extend previous range if contiguous
  // C++ code directly compares usage_page values, even if they are 0
  if (state->num_usage_ranges > 0)
  {
    usage_range_t *r = &state->usage_ranges[state->num_usage_ranges - 1];
    // Direct comparison like C++ code - both can be 0
    // Only extend if pages match and ranges are contiguous
    // But don't extend single-value ranges (separate USAGE items)
    if (r->usage_page == usage_page && r->usage_max + 1 == usage_min)
    {
      // Only extend if the previous range was not a single-value range
      // OR if the new range is also not a single-value range
      // This allows USAGE_MIN/USAGE_MAX ranges to be extended, but not separate USAGE items
      if (r->usage_min != r->usage_max || usage_min != usage_max)
      {
        r->usage_max = usage_max;
        return true;
      }
    }
  }

  if (state->num_usage_ranges >= MAX_USAGE_RANGES)
    return false;

  usage_range_t *r = &state->usage_ranges[state->num_usage_ranges++];
  r->usage_min = usage_min;
  r->usage_max = usage_max;
  r->usage_page = usage_page; // Can be 0, will be set later
  return true;
}

// Process input field - following C++ AddField logic exactly
static int process_input_field(parser_state_t *state, const uint8_t *p_data, uint8_t data_size, uint16_t flags)
{
  if (!state->current_layout)
  {
    return 0; // No layout to fill
  }

  // Calculate bit_size exactly like C++
  uint32_t bit_size = state->report_size * state->report_count;

  // HID specification: If an item has no controls (Report Count = 0),
  // the Local item tags apply to the Main item (usually a collection item).
  // We silently ignore this zero sized "field".
  if (bit_size == 0)
    return 0;

  // Check report ID consistency
  if (state->first_field_processed)
  {
    if (state->first_field_has_report_id != (state->report_id != 0))
    {
      return -1; // Bad report ID assignment
    }
  }
  else
  {
    state->first_field_has_report_id = (state->report_id != 0);
    state->first_field_processed = true;
  }

  // If no usage ranges, this is padding - skip it but advance bit offset
  if (!state->num_usage_ranges)
  {
    state->current_bit_offset += bit_size;
    return 0;
  }

  // Check logical min/max
  if ((state->logical_min < 0 && state->logical_max < state->logical_min) ||
      (state->logical_min >= 0 && (uint32_t)state->logical_max < (uint32_t)state->logical_min))
  {
    return -1; // Logical min is greater than max
  }

  // Read flags
  uint16_t field_flags = 0;
  if (!uint16_data(p_data, data_size, &field_flags, true))
  {
    return -1;
  }

  // CRITICAL: In case of an extended USAGE or USAGE_MIN/MAX the usage_page field is
  // set immediately. A normal USAGE or USAGE_MIN/MAX item leaves the usage_page zero
  // and the parser sets it to the value of _globals.usage_page when a main item is
  // encountered as per specification.
  // This is done BEFORE processing the field!
  for (int i = 0; i < state->num_usage_ranges; i++)
  {
    if (state->usage_ranges[i].usage_page == 0)
    {
      if (state->usage_page == 0)
      {
        return -1; // Undefined usage page
      }
      state->usage_ranges[i].usage_page = state->usage_page;
    }
  }

  // Now process the field
  bool is_variable = (field_flags & FLAG_FIELD_VARIABLE) != 0;
  bool is_relative = (field_flags & FLAG_FIELD_RELATIVE) != 0;

  // Check if this field has mouse-related usages
  bool has_mouse_usage = false;
  if (state->in_mouse_collection)
  {
    has_mouse_usage = true;
  }
  else
  {
    // Check if any usage in usage_ranges is mouse-related
    for (int i = 0; i < state->num_usage_ranges; i++)
    {
      uint16_t page = state->usage_ranges[i].usage_page;
      if (page == PAGE_GENERIC_DESKTOP || page == PAGE_BUTTON || page == PAGE_CONSUMER)
      {
        has_mouse_usage = true;
        break;
      }
    }
  }

  // Skip non-mouse fields
  if (!has_mouse_usage)
  {
    state->current_bit_offset += bit_size;
    return 0;
  }

  // Process each usage range
  // For variable fields: each usage_range corresponds to one item in report_count
  // For array fields: usages map to array indices
  uint32_t usage_index = 0;
  for (int i = 0; i < state->num_usage_ranges; i++)
  {
    uint16_t page = state->usage_ranges[i].usage_page;
    uint16_t usage_min = state->usage_ranges[i].usage_min;
    uint16_t usage_max = state->usage_ranges[i].usage_max;

    if (is_variable)
    {
      // For variable fields, each usage_range gets one usage_index
      // We use usage_min as the representative usage for the range
      // usage_index corresponds to the position in report_count
      if (usage_index >= state->report_count)
        break; // No more slots in report_count

      uint32_t field_bit_offset = state->current_bit_offset + (usage_index * state->report_size);
      usage_index++;

      // Process buttons (Button Page)
      if (page == PAGE_BUTTON && usage_min >= 1)
      {
        if (!state->layout_valid)
        {
          state->current_layout->report_id = state->report_id;
          state->current_layout->buttons_bit_offset = state->current_bit_offset;
          state->current_layout->buttons_count = state->report_count;
          state->layout_valid = true;
        }
        else
        {
          // Extend button count if needed
          if (state->report_count > state->current_layout->buttons_count)
          {
            state->current_layout->buttons_count = state->report_count;
          }
        }
      }

      // Process X axis
      if (page == PAGE_GENERIC_DESKTOP && usage_min == USAGE_X)
      {
        state->current_layout->x_bit_offset = field_bit_offset;
        state->current_layout->x_size = state->report_size;
        state->layout_valid = true;
      }

      // Process Y axis
      if (page == PAGE_GENERIC_DESKTOP && usage_min == USAGE_Y)
      {
        state->current_layout->y_bit_offset = field_bit_offset;
        state->current_layout->y_size = state->report_size;
        state->layout_valid = true;
      }

      // Process wheel (vertical scroll)
      if (page == PAGE_GENERIC_DESKTOP && usage_min == USAGE_WHEEL)
      {
        state->current_layout->wheel_bit_offset = field_bit_offset;
        state->current_layout->wheel_size = state->report_size;
        state->layout_valid = true;
      }

      // Process pan (horizontal scroll) - Consumer Page
      if (page == PAGE_CONSUMER && usage_min == USAGE_CONSUMER_AC_PAN)
      {
        state->current_layout->pan_bit_offset = field_bit_offset;
        state->current_layout->pan_size = state->report_size;
        state->layout_valid = true;
      }
    }
    else
    {
      // Array field - all usages share the same offset
      // Check if the range contains any mouse-related usage
      uint16_t range_end = usage_max < usage_min ? usage_min : usage_max;
      uint32_t field_bit_offset = state->current_bit_offset;

      // Process buttons (Button Page)
      if (page == PAGE_BUTTON && usage_min >= 1)
      {
        uint32_t range_length = (uint32_t)range_end - (uint32_t)usage_min + 1;
        if (!state->layout_valid)
        {
          state->current_layout->report_id = state->report_id;
          state->current_layout->buttons_bit_offset = field_bit_offset;
          state->current_layout->buttons_count = range_length;
          state->layout_valid = true;
        }
        else
        {
          // Extend button count if needed
          if (range_length > state->current_layout->buttons_count)
          {
            state->current_layout->buttons_count = range_length;
          }
        }
      }

      // Process X axis - check if range contains USAGE_X
      if (page == PAGE_GENERIC_DESKTOP && usage_min <= USAGE_X && USAGE_X <= range_end)
      {
        state->current_layout->x_bit_offset = field_bit_offset;
        state->current_layout->x_size = state->report_size;
        state->layout_valid = true;
      }

      // Process Y axis - check if range contains USAGE_Y
      if (page == PAGE_GENERIC_DESKTOP && usage_min <= USAGE_Y && USAGE_Y <= range_end)
      {
        state->current_layout->y_bit_offset = field_bit_offset;
        state->current_layout->y_size = state->report_size;
        state->layout_valid = true;
      }

      // Process wheel (vertical scroll) - check if range contains USAGE_WHEEL
      if (page == PAGE_GENERIC_DESKTOP && usage_min <= USAGE_WHEEL && USAGE_WHEEL <= range_end)
      {
        state->current_layout->wheel_bit_offset = field_bit_offset;
        state->current_layout->wheel_size = state->report_size;
        state->layout_valid = true;
      }

      // Process pan (horizontal scroll) - Consumer Page
      if (page == PAGE_CONSUMER && usage_min <= USAGE_CONSUMER_AC_PAN && USAGE_CONSUMER_AC_PAN <= range_end)
      {
        state->current_layout->pan_bit_offset = field_bit_offset;
        state->current_layout->pan_size = state->report_size;
        state->layout_valid = true;
      }
    }
  }

  state->current_bit_offset += bit_size;
  return 0;
}

static int parse_main_item(parser_state_t *state, uint8_t item, const uint8_t *p_data, uint8_t data_size)
{
  switch (item)
  {
  case ITEM_COLLECTION:
  {
    uint8_t collection_type;
    if (!uint8_data(p_data, data_size, &collection_type))
    {
      return -1;
    }
    state->collection_depth++;

    // Check if this is a mouse collection
    // Use FirstUsage() logic: get first usage from usage_ranges
    if (collection_type == COLLECTION_TYPE_APPLICATION && state->num_usage_ranges > 0)
    {
      uint16_t first_usage = state->usage_ranges[0].usage_min;
      uint16_t first_page = state->usage_ranges[0].usage_page;
      if (first_page == 0)
        first_page = state->usage_page;

      if (first_page == PAGE_GENERIC_DESKTOP && first_usage == USAGE_MOUSE)
      {
        state->in_mouse_collection = true;
      }
    }
    return 0;
  }

  case ITEM_END_COLLECTION:
    if (state->collection_depth == 0)
    {
      return -1;
    }
    state->collection_depth--;
    if (state->collection_depth == 0)
    {
      state->in_mouse_collection = false;
    }
    return 0;

  case ITEM_INPUT:
  {
    uint16_t flags = 0;
    if (!uint16_data(p_data, data_size, &flags, true))
    {
      return -1;
    }
    int res = process_input_field(state, p_data, data_size, flags);
    // Reset locals after processing INPUT (like C++ does)
    reset_locals(state);
    return res;
  }

  case ITEM_OUTPUT:
  case ITEM_FEATURE:
    // Skip output and feature items, but still advance bit offset
    // Reset locals after processing (like C++ does)
    reset_locals(state);
    return 0;

  default:
    return 0;
  }
}

static int parse_global_item(parser_state_t *state, uint8_t item, const uint8_t *p_data, uint8_t data_size)
{
  switch (item)
  {
  case ITEM_USAGE_PAGE:
    return uint16_data(p_data, data_size, &state->usage_page, false) ? 0 : -1;

  case ITEM_LOGICAL_MIN:
    return int32_data(p_data, data_size, &state->logical_min) ? 0 : -1;

  case ITEM_LOGICAL_MAX:
  {
    // Workaround for sign extension issues (exactly like C++)
    if (state->logical_min >= 0)
    {
      uint32_t max_u32;
      if (!uint32_data(p_data, data_size, &max_u32))
        return -1;
      state->logical_max = (int32_t)max_u32;
    }
    else
    {
      if (!int32_data(p_data, data_size, &state->logical_max))
        return -1;
      if (state->logical_max < state->logical_min)
      {
        uint32_t max_u32;
        if (!uint32_data(p_data, data_size, &max_u32))
          return -1;
        state->logical_max = (int32_t)max_u32;
      }
    }
    return 0;
  }

  case ITEM_REPORT_SIZE:
    return uint32_data(p_data, data_size, &state->report_size) ? 0 : -1;

  case ITEM_REPORT_ID:
    return uint8_data(p_data, data_size, &state->report_id) ? 0 : -1;

  case ITEM_REPORT_COUNT:
    return uint32_data(p_data, data_size, &state->report_count) ? 0 : -1;

  case ITEM_PUSH:
    if (state->global_stack_size >= MAX_PUSH_POP_STACK)
    {
      return -1;
    }
    state->global_stack[state->global_stack_size].report_id = state->report_id;
    state->global_stack[state->global_stack_size].usage_page = state->usage_page;
    state->global_stack[state->global_stack_size].logical_min = state->logical_min;
    state->global_stack[state->global_stack_size].logical_max = state->logical_max;
    state->global_stack[state->global_stack_size].report_size = state->report_size;
    state->global_stack[state->global_stack_size].report_count = state->report_count;
    state->global_stack_size++;
    return 0;

  case ITEM_POP:
    if (state->global_stack_size == 0)
    {
      return -1;
    }
    state->global_stack_size--;
    state->report_id = state->global_stack[state->global_stack_size].report_id;
    state->usage_page = state->global_stack[state->global_stack_size].usage_page;
    state->logical_min = state->global_stack[state->global_stack_size].logical_min;
    state->logical_max = state->global_stack[state->global_stack_size].logical_max;
    state->report_size = state->global_stack[state->global_stack_size].report_size;
    state->report_count = state->global_stack[state->global_stack_size].report_count;
    return 0;

  default:
    return 0;
  }
}

// ParseLocalItems: exactly like C++ version
static int parse_local_item(parser_state_t *state, uint8_t item, const uint8_t *p_data, uint8_t data_size)
{
  uint16_t usage, usage_page;

  switch (item)
  {
  case ITEM_USAGE:
    if (!usage_data(p_data, data_size, &usage, &usage_page))
    {
      return -1;
    }
    if (!add_usage_range(state, usage, usage, usage_page))
    {
      return -1; // Too many usages
    }
    return 0;

  case ITEM_USAGE_MIN:
    if (!usage_data(p_data, data_size, &usage, &usage_page))
    {
      return -1;
    }

    switch (state->flags & (FLAG_USAGE_MIN | FLAG_USAGE_MAX))
    {
    case FLAG_USAGE_MIN:
      // Overwriting the previous USAGE_MIN that wasn't closed with a USAGE_MAX
      {
        usage_range_t *r = &state->usage_ranges[state->num_usage_ranges - 1];
        r->usage_min = usage;
        r->usage_page = usage_page;
      }
      break;

    case FLAG_USAGE_MAX:
    {
      usage_range_t *r = &state->usage_ranges[state->num_usage_ranges - 1];
      uint16_t r_page = r->usage_page;
      if (r_page == 0)
        r_page = state->usage_page;
      uint16_t new_page = usage_page;
      if (new_page == 0)
        new_page = state->usage_page;
      if (r_page != new_page)
      {
        return -1; // Page mismatch
      }
      if (usage > r->usage_max)
      {
        return -1; // Invalid range
      }
      r->usage_min = usage;
      state->flags &= ~FLAG_USAGE_MAX;
    }
    break;

    case 0:
      if (!add_usage_range(state, usage, usage, usage_page))
      {
        return -1;
      }
      state->flags |= FLAG_USAGE_MIN;
      break;
    }
    return 0;

  case ITEM_USAGE_MAX:
    if (!usage_data(p_data, data_size, &usage, &usage_page))
    {
      return -1;
    }

    switch (state->flags & (FLAG_USAGE_MIN | FLAG_USAGE_MAX))
    {
    case FLAG_USAGE_MAX:
      // Overwriting the previous USAGE_MAX that wasn't closed with a USAGE_MIN
      {
        usage_range_t *r = &state->usage_ranges[state->num_usage_ranges - 1];
        r->usage_max = usage;
        r->usage_page = usage_page;
      }
      break;

    case FLAG_USAGE_MIN:
    {
      usage_range_t *r = &state->usage_ranges[state->num_usage_ranges - 1];
      uint16_t r_page = r->usage_page;
      if (r_page == 0)
        r_page = state->usage_page;
      uint16_t new_page = usage_page;
      if (new_page == 0)
        new_page = state->usage_page;
      if (r_page != new_page)
      {
        return -1; // Page mismatch
      }
      if (usage < r->usage_min)
      {
        return -1; // Invalid range
      }
      r->usage_max = usage;
      state->flags &= ~FLAG_USAGE_MIN;
    }
    break;

    case 0:
      if (!add_usage_range(state, usage, usage, usage_page))
      {
        return -1;
      }
      state->flags |= FLAG_USAGE_MAX;
      break;
    }
    return 0;

  default:
    return 0;
  }
}

// Structure to track layouts per report ID
typedef struct
{
  uint8_t report_id;
  hid_report_layout_t layout;
  bool valid;
} layout_tracker_t;

static int find_or_create_layout(layout_tracker_t *trackers, int *count, int max_count, uint8_t report_id)
{
  // Find existing layout for this report ID
  for (int i = 0; i < *count; i++)
  {
    if (trackers[i].report_id == report_id)
    {
      return i;
    }
  }

  // Create new layout
  if (*count >= max_count)
  {
    return -1;
  }

  int idx = (*count)++;
  trackers[idx].report_id = report_id;
  memset(&trackers[idx].layout, 0, sizeof(hid_report_layout_t));
  trackers[idx].layout.report_id = report_id;
  trackers[idx].valid = false;
  return idx;
}

int parse_hid_report_descriptor_layouts(const void *descriptor, size_t descriptor_size,
                                        hid_report_layout_t *layouts, int max_layouts)
{
  if (!descriptor || !layouts || max_layouts <= 0)
  {
    return 0;
  }

  // Initialize layouts
  for (int i = 0; i < max_layouts; i++)
  {
    memset(&layouts[i], 0, sizeof(hid_report_layout_t));
  }

  parser_state_t state;
  layout_tracker_t trackers[16];
  int tracker_count = 0;
  int current_tracker_idx = -1;

  reset_parser(&state);

  const uint8_t *p = (const uint8_t *)descriptor;
  const uint8_t *q = p + descriptor_size;

  // Start with report ID 0 (no report ID)
  current_tracker_idx = find_or_create_layout(trackers, &tracker_count, max_layouts, 0);
  if (current_tracker_idx >= 0)
  {
    state.current_layout = &trackers[current_tracker_idx].layout;
    state.current_layout->report_id = 0;
  }

  while (p < q)
  {
    uint8_t b = *p++;
    size_t bytes_left = q - p;

    if (b == ITEM_LONG)
    {
      if (bytes_left < 1)
        break;
      p += 2 + (size_t)*p;
      continue;
    }

    uint8_t data_size = b & ITEM_SIZE_MASK;
    if (data_size == 3)
      data_size = 4;
    if (bytes_left < data_size)
      break;

    uint8_t item = b & ITEM_TAG_AND_TYPE_MASK;

    // Process the item first (to update state)
    int res = 0;
    switch (b & ITEM_TYPE_MASK)
    {
    case ITEM_TYPE_MAIN:
      res = parse_main_item(&state, item, p, data_size);
      break;

    case ITEM_TYPE_GLOBAL:
      res = parse_global_item(&state, item, p, data_size);
      // Handle REPORT_ID global item - switch to new layout tracker AFTER processing
      if (item == ITEM_REPORT_ID && state.report_id != 0)
      {
        // Save current layout state
        if (current_tracker_idx >= 0 && state.current_layout)
        {
          bool has_fields = (state.current_layout->buttons_count > 0 ||
                             state.current_layout->x_size > 0 ||
                             state.current_layout->y_size > 0 ||
                             state.current_layout->wheel_size > 0 ||
                             state.current_layout->pan_size > 0);

          if (state.layout_valid || has_fields)
          {
            trackers[current_tracker_idx].layout = *state.current_layout;
            trackers[current_tracker_idx].layout.report_size_bits = state.current_bit_offset;
            trackers[current_tracker_idx].valid = true;
          }
        }

        // Switch to new report ID layout
        int idx = find_or_create_layout(trackers, &tracker_count, max_layouts, state.report_id);
        if (idx >= 0)
        {
          current_tracker_idx = idx;
          state.current_layout = &trackers[idx].layout;
          state.current_layout->report_id = state.report_id;
          // Reset parser state for new report (except globals that persist)
          state.current_bit_offset = 0;
          state.first_field_processed = false;
          state.first_field_has_report_id = false;
          state.layout_valid = false;
          reset_locals(&state);
        }
      }
      break;

    case ITEM_TYPE_LOCAL:
      res = parse_local_item(&state, item, p, data_size);
      break;

    default:
      break;
    }

    if (res != 0)
      break;
    p += data_size;
  }

  // Save final layout state
  if (current_tracker_idx >= 0 && state.current_layout)
  {
    bool has_fields = (state.current_layout->buttons_count > 0 ||
                       state.current_layout->x_size > 0 ||
                       state.current_layout->y_size > 0 ||
                       state.current_layout->wheel_size > 0 ||
                       state.current_layout->pan_size > 0);

    if (state.layout_valid || has_fields)
    {
      trackers[current_tracker_idx].layout = *state.current_layout;
      trackers[current_tracker_idx].layout.report_size_bits = state.current_bit_offset;
      trackers[current_tracker_idx].valid = true;
    }
  }

  // Copy valid layouts to output array
  int layout_count = 0;
  for (int i = 0; i < tracker_count && layout_count < max_layouts; i++)
  {
    bool has_fields = (trackers[i].layout.buttons_count > 0 ||
                       trackers[i].layout.x_size > 0 ||
                       trackers[i].layout.y_size > 0 ||
                       trackers[i].layout.wheel_size > 0 ||
                       trackers[i].layout.pan_size > 0);

    if (trackers[i].valid || has_fields)
    {
      layouts[layout_count] = trackers[i].layout;
      layout_count++;
    }
  }

  return layout_count;
}

int parse_hid_report_descriptor_layout(const void *descriptor, size_t descriptor_size,
                                       hid_report_layout_t *layout)
{
  if (!descriptor || !layout)
  {
    return -1;
  }

  parser_state_t state;
  reset_parser(&state);
  state.current_layout = layout;
  layout->report_id = 0;

  const uint8_t *p = (const uint8_t *)descriptor;
  const uint8_t *q = p + descriptor_size;

  while (p < q)
  {
    uint8_t b = *p++;
    size_t bytes_left = q - p;

    if (b == ITEM_LONG)
    {
      if (bytes_left < 1)
        break;
      p += 2 + (size_t)*p;
      continue;
    }

    uint8_t data_size = b & ITEM_SIZE_MASK;
    if (data_size == 3)
      data_size = 4;
    if (bytes_left < data_size)
      break;

    uint8_t item = b & ITEM_TAG_AND_TYPE_MASK;
    int res = 0;

    switch (b & ITEM_TYPE_MASK)
    {
    case ITEM_TYPE_MAIN:
      res = parse_main_item(&state, item, p, data_size);
      break;

    case ITEM_TYPE_GLOBAL:
      res = parse_global_item(&state, item, p, data_size);
      break;

    case ITEM_TYPE_LOCAL:
      res = parse_local_item(&state, item, p, data_size);
      break;

    default:
      break;
    }

    if (res != 0)
      break;
    p += data_size;
  }

  if (state.layout_valid)
  {
    layout->report_size_bits = state.current_bit_offset;
    return 0;
  }

  return -1;
}
