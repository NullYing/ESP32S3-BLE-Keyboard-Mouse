#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include <stdbool.h>
#include "hid_report_parser.h"

static const char *TAG = "HID_PARSER";

/**
 * @brief Parse and print HID Report Descriptor
 *
 * @param[in] desc    Pointer to HID Report Descriptor data
 * @param[in] length  Length of descriptor data
 */
void parse_hid_report_descriptor(const uint8_t *desc, size_t length)
{
    if (desc == NULL || length == 0)
    {
        ESP_LOGW(TAG, "Invalid descriptor data");
        return;
    }

    ESP_LOGI(TAG, "\n========== HID Report Descriptor 解析 ==========");

    size_t offset = 0;
    int indent_level = 0;
    uint32_t usage_page = 0;
    uint32_t logical_min = 0, logical_max = 0;
    uint32_t physical_min = 0, physical_max = 0;
    uint32_t report_size = 0, report_count = 0;
    uint32_t unit = 0, unit_exponent = 0;

    while (offset < length)
    {
        uint8_t item = desc[offset];
        uint8_t item_type = (item >> 2) & 0x03; // 0=Main, 1=Global, 2=Local, 3=Reserved
        uint8_t item_tag = (item >> 4) & 0x0F;
        uint8_t item_size = item & 0x03; // 0=0字节, 1=1字节, 2=2字节, 3=4字节

        if (offset + item_size >= length)
        {
            ESP_LOGW(TAG, "Descriptor truncated at offset %zu", offset);
            break;
        }

        uint32_t item_value = 0;
        if (item_size == 1)
        {
            item_value = desc[offset + 1];
        }
        else if (item_size == 2)
        {
            item_value = desc[offset + 1] | (desc[offset + 2] << 8);
        }
        else if (item_size == 3)
        {
            item_value = desc[offset + 1] | (desc[offset + 2] << 8) |
                         (desc[offset + 3] << 16) | (desc[offset + 4] << 24);
        }

        // 打印缩进
        for (int i = 0; i < indent_level; i++)
        {
            printf("  ");
        }

        // 解析不同类型的项目
        if (item_type == 0) // Main Items
        {
            switch (item_tag)
            {
            case 0x08: // Input
                printf("Input(");
                if (item_value & 0x01)
                    printf("Data,");
                else
                    printf("Constant,");
                if (item_value & 0x02)
                    printf("Variable,");
                else
                    printf("Array,");
                if (item_value & 0x04)
                    printf("Absolute,");
                else
                    printf("Relative,");
                if (item_value & 0x08)
                    printf("Wrap,");
                if (item_value & 0x10)
                    printf("Non-linear,");
                if (item_value & 0x20)
                    printf("No preferred state,");
                if (item_value & 0x40)
                    printf("Null state,");
                if (item_value & 0x80)
                    printf("Volatile,");
                printf(")");
                printf(" // Size=%" PRIu32 ", Count=%" PRIu32, report_size, report_count);
                break;
            case 0x09: // Output
                printf("Output(");
                if (item_value & 0x01)
                    printf("Data,");
                else
                    printf("Constant,");
                if (item_value & 0x02)
                    printf("Variable,");
                else
                    printf("Array,");
                if (item_value & 0x04)
                    printf("Absolute,");
                else
                    printf("Relative,");
                if (item_value & 0x08)
                    printf("Wrap,");
                if (item_value & 0x10)
                    printf("Non-linear,");
                if (item_value & 0x20)
                    printf("No preferred state,");
                if (item_value & 0x40)
                    printf("Null state,");
                if (item_value & 0x80)
                    printf("Volatile,");
                printf(")");
                printf(" // Size=%" PRIu32 ", Count=%" PRIu32, report_size, report_count);
                break;
            case 0x0B: // Feature
                printf("Feature(");
                if (item_value & 0x01)
                    printf("Data,");
                else
                    printf("Constant,");
                if (item_value & 0x02)
                    printf("Variable,");
                else
                    printf("Array,");
                if (item_value & 0x04)
                    printf("Absolute,");
                else
                    printf("Relative,");
                if (item_value & 0x08)
                    printf("Wrap,");
                if (item_value & 0x10)
                    printf("Non-linear,");
                if (item_value & 0x20)
                    printf("No preferred state,");
                if (item_value & 0x40)
                    printf("Null state,");
                if (item_value & 0x80)
                    printf("Volatile,");
                printf(")");
                printf(" // Size=%" PRIu32 ", Count=%" PRIu32, report_size, report_count);
                break;
            case 0x0A: // Collection
                printf("Collection(");
                switch (item_value)
                {
                case 0x00:
                    printf("Physical");
                    break;
                case 0x01:
                    printf("Application");
                    break;
                case 0x02:
                    printf("Logical");
                    break;
                case 0x03:
                    printf("Report");
                    break;
                case 0x04:
                    printf("Named Array");
                    break;
                case 0x05:
                    printf("Usage Switch");
                    break;
                case 0x06:
                    printf("Usage Modifier");
                    break;
                default:
                    printf("0x%02X", (uint8_t)item_value);
                    break;
                }
                printf(")");
                indent_level++;
                break;
            case 0x0C: // End Collection
                indent_level--;
                if (indent_level < 0)
                    indent_level = 0;
                printf("End Collection");
                break;
            default:
                printf("Main Item(0x%02X, 0x%02X)", item, (uint8_t)item_value);
                break;
            }
        }
        else if (item_type == 1) // Global Items
        {
            switch (item_tag)
            {
            case 0x00: // Usage Page
                usage_page = item_value;
                printf("Usage Page(");
                switch (usage_page)
                {
                case 0x01:
                    printf("Generic Desktop");
                    break;
                case 0x02:
                    printf("Simulation Controls");
                    break;
                case 0x03:
                    printf("VR Controls");
                    break;
                case 0x04:
                    printf("Sport Controls");
                    break;
                case 0x05:
                    printf("Game Controls");
                    break;
                case 0x06:
                    printf("Generic Device Controls");
                    break;
                case 0x07:
                    printf("Keyboard/Keypad");
                    break;
                case 0x08:
                    printf("LEDs");
                    break;
                case 0x09:
                    printf("Button");
                    break;
                case 0x0A:
                    printf("Ordinal");
                    break;
                case 0x0B:
                    printf("Telephony");
                    break;
                case 0x0C:
                    printf("Consumer");
                    break;
                case 0x0D:
                    printf("Digitizer");
                    break;
                case 0x0E:
                    printf("Haptics");
                    break;
                case 0x0F:
                    printf("PID Page");
                    break;
                case 0x10:
                    printf("Unicode");
                    break;
                case 0x14:
                    printf("Eye and Head Trackers");
                    break;
                case 0x40:
                    printf("Vendor-defined 0x40");
                    break;
                case 0x80:
                    printf("Vendor-defined 0x80");
                    break;
                case 0x83:
                    printf("Reserved");
                    break;
                case 0x84:
                    printf("Power");
                    break;
                case 0x85:
                    printf("Battery System");
                    break;
                case 0x8C:
                    printf("Bar Code Scanner");
                    break;
                case 0x8D:
                    printf("Scale");
                    break;
                case 0x8E:
                    printf("Magnetic Stripe Reader");
                    break;
                case 0x8F:
                    printf("Reserved Point of Sale");
                    break;
                case 0x90:
                    printf("Camera Control");
                    break;
                case 0x91:
                    printf("Arcade");
                    break;
                default:
                    printf("0x%04X", (uint16_t)usage_page);
                    break;
                }
                printf(")");
                break;
            case 0x01: // Logical Minimum
                logical_min = item_value;
                // 根据数据大小判断是否为有符号数
                if (item_size == 1 && (item_value & 0x80))
                {
                    int8_t signed_min = (int8_t)(uint8_t)item_value;
                    printf("Logical Minimum(%d)", (int)signed_min);
                }
                else if (item_size == 2 && (item_value & 0x8000))
                {
                    int16_t signed_min = (int16_t)(uint16_t)item_value;
                    printf("Logical Minimum(%d)", (int)signed_min);
                }
                else if (item_size == 3 && (item_value & 0x80000000))
                {
                    int32_t signed_min = (int32_t)item_value;
                    printf("Logical Minimum(%" PRId32 ")", signed_min);
                }
                else
                {
                    printf("Logical Minimum(%" PRIu32 ")", logical_min);
                }
                break;
            case 0x02: // Logical Maximum
                logical_max = item_value;
                // 根据数据大小判断是否为有符号数
                if (item_size == 1 && (item_value & 0x80))
                {
                    int8_t signed_max = (int8_t)(uint8_t)item_value;
                    printf("Logical Maximum(%d)", (int)signed_max);
                }
                else if (item_size == 2 && (item_value & 0x8000))
                {
                    int16_t signed_max = (int16_t)(uint16_t)item_value;
                    printf("Logical Maximum(%d)", (int)signed_max);
                }
                else if (item_size == 3 && (item_value & 0x80000000))
                {
                    int32_t signed_max = (int32_t)item_value;
                    printf("Logical Maximum(%" PRId32 ")", signed_max);
                }
                else
                {
                    printf("Logical Maximum(%" PRIu32 ")", logical_max);
                }
                break;
            case 0x03: // Physical Minimum
                physical_min = item_value;
                printf("Physical Minimum(%" PRIu32 ")", physical_min);
                break;
            case 0x04: // Physical Maximum
                physical_max = item_value;
                printf("Physical Maximum(%" PRIu32 ")", physical_max);
                break;
            case 0x05: // Unit Exponent
                unit_exponent = item_value;
                printf("Unit Exponent(%" PRIu32 ")", unit_exponent);
                break;
            case 0x06: // Unit
                unit = item_value;
                printf("Unit(0x%08" PRIX32 ")", (unsigned long)unit);
                break;
            case 0x07: // Report Size
                report_size = item_value;
                printf("Report Size(%" PRIu32 ")", report_size);
                break;
            case 0x08: // Report Count
                report_count = item_value;
                printf("Report Count(%" PRIu32 ")", report_count);
                break;
            case 0x09: // Report ID
                printf("Report ID(%" PRIu32 ")", item_value);
                break;
            case 0x0A: // Push
                printf("Push");
                break;
            case 0x0B: // Pop
                printf("Pop");
                break;
            default:
                printf("Global Item(0x%02X, 0x%02X)", item, (uint8_t)item_value);
                break;
            }
        }
        else if (item_type == 2) // Local Items
        {
            switch (item_tag)
            {
            case 0x00: // Usage
                printf("Usage(");
                if (usage_page == 0x01) // Generic Desktop
                {
                    switch (item_value)
                    {
                    case 0x00:
                        printf("Undefined");
                        break;
                    case 0x01:
                        printf("Pointer");
                        break;
                    case 0x02:
                        printf("Mouse");
                        break;
                    case 0x04:
                        printf("Joystick");
                        break;
                    case 0x05:
                        printf("Game Pad");
                        break;
                    case 0x06:
                        printf("Keyboard");
                        break;
                    case 0x07:
                        printf("Keypad");
                        break;
                    case 0x08:
                        printf("Multi-axis Controller");
                        break;
                    case 0x30:
                        printf("X");
                        break;
                    case 0x31:
                        printf("Y");
                        break;
                    case 0x32:
                        printf("Z");
                        break;
                    case 0x33:
                        printf("Rx");
                        break;
                    case 0x34:
                        printf("Ry");
                        break;
                    case 0x35:
                        printf("Rz");
                        break;
                    case 0x36:
                        printf("Slider");
                        break;
                    case 0x37:
                        printf("Dial");
                        break;
                    case 0x38:
                        printf("Wheel");
                        break;
                    case 0x39:
                        printf("Hat Switch");
                        break;
                    case 0x3A:
                        printf("Counted Buffer");
                        break;
                    case 0x3B:
                        printf("Byte Count");
                        break;
                    case 0x3C:
                        printf("Motion Wakeup");
                        break;
                    case 0x3D:
                        printf("Start");
                        break;
                    case 0x3E:
                        printf("Select");
                        break;
                    case 0x40:
                        printf("Vx");
                        break;
                    case 0x41:
                        printf("Vy");
                        break;
                    case 0x42:
                        printf("Vz");
                        break;
                    case 0x43:
                        printf("Vbrx");
                        break;
                    case 0x44:
                        printf("Vbry");
                        break;
                    case 0x45:
                        printf("Vbrz");
                        break;
                    case 0x46:
                        printf("Vno");
                        break;
                    case 0x80:
                        printf("System Control");
                        break;
                    case 0x81:
                        printf("System Power Down");
                        break;
                    case 0x82:
                        printf("System Sleep");
                        break;
                    case 0x83:
                        printf("System Wake Up");
                        break;
                    case 0x84:
                        printf("System Context Menu");
                        break;
                    case 0x85:
                        printf("System Main Menu");
                        break;
                    case 0x86:
                        printf("System App Menu");
                        break;
                    case 0x87:
                        printf("System Menu Help");
                        break;
                    case 0x88:
                        printf("System Menu Exit");
                        break;
                    case 0x89:
                        printf("System Menu Select");
                        break;
                    case 0x8A:
                        printf("System Menu Right");
                        break;
                    case 0x8B:
                        printf("System Menu Left");
                        break;
                    case 0x8C:
                        printf("System Menu Up");
                        break;
                    case 0x8D:
                        printf("System Menu Down");
                        break;
                    case 0x90:
                        printf("D-pad Up");
                        break;
                    case 0x91:
                        printf("D-pad Down");
                        break;
                    case 0x92:
                        printf("D-pad Right");
                        break;
                    case 0x93:
                        printf("D-pad Left");
                        break;
                    default:
                        printf("0x%04X", (uint16_t)item_value);
                        break;
                    }
                }
                else if (usage_page == 0x09) // Button
                {
                    printf("Button %" PRIu32, item_value);
                }
                else if (usage_page == 0x07) // Keyboard/Keypad
                {
                    printf("0x%04X", (uint16_t)item_value);
                }
                else
                {
                    printf("0x%04X", (uint16_t)item_value);
                }
                printf(")");
                break;
            case 0x01: // Usage Minimum
                printf("Usage Minimum(");
                if (usage_page == 0x09) // Button
                {
                    printf("Button %" PRIu32, item_value);
                }
                else
                {
                    printf("0x%04X", (uint16_t)item_value);
                }
                printf(")");
                break;
            case 0x02: // Usage Maximum
                printf("Usage Maximum(");
                if (usage_page == 0x09) // Button
                {
                    printf("Button %" PRIu32, item_value);
                }
                else
                {
                    printf("0x%04X", (uint16_t)item_value);
                }
                printf(")");
                break;
            case 0x03: // Designator Index
                printf("Designator Index(%" PRIu32 ")", item_value);
                break;
            case 0x04: // Designator Minimum
                printf("Designator Minimum(%" PRIu32 ")", item_value);
                break;
            case 0x05: // Designator Maximum
                printf("Designator Maximum(%" PRIu32 ")", item_value);
                break;
            case 0x07: // String Index
                printf("String Index(%" PRIu32 ")", item_value);
                break;
            case 0x08: // String Minimum
                printf("String Minimum(%" PRIu32 ")", item_value);
                break;
            case 0x09: // String Maximum
                printf("String Maximum(%" PRIu32 ")", item_value);
                break;
            case 0x0A: // Delimiter
                printf("Delimiter(%" PRIu32 ")", item_value);
                break;
            default:
                printf("Local Item(0x%02X, 0x%02X)", item, (uint8_t)item_value);
                break;
            }
        }
        else
        {
            printf("Reserved Item(0x%02X, 0x%02X)", item, (uint8_t)item_value);
        }

        // 打印原始字节
        printf(" // ");
        for (int i = 0; i <= item_size; i++)
        {
            printf("%02X ", desc[offset + i]);
        }
        printf("\n");

        offset += item_size + 1;
    }

    ESP_LOGI(TAG, "========== 解析完成 ==========\n");
}

int parse_hid_report_descriptor_layout(const uint8_t *desc, size_t length, hid_report_layout_t *out_layout)
{
    if (desc == NULL || length == 0 || out_layout == NULL)
    {
        return -1;
    }
    // We'll collect per-report information and then select appropriate layout(s).
    // Support up to 256 report IDs (0..255)
    hid_report_layout_t tmp_layouts[256];
    uint8_t report_used[256] = {0};
    uint32_t report_bits[256];
    for (int i = 0; i < 256; i++)
    {
        memset(&tmp_layouts[i], 0, sizeof(hid_report_layout_t));
        report_bits[i] = 0;
    }

    size_t offset = 0;
    uint32_t usage_page = 0;
    uint32_t report_size = 0, report_count = 0;
    uint8_t current_report_id = 0;

    // Local items
    uint32_t last_usage = 0;
    uint32_t usage_min = 0, usage_max = 0;
    bool have_usage_min_max = false;

    while (offset < length)
    {
        uint8_t item = desc[offset];
        uint8_t item_type = (item >> 2) & 0x03; // 0=Main,1=Global,2=Local
        uint8_t item_tag = (item >> 4) & 0x0F;
        uint8_t item_size = item & 0x03; // 0,1,2,3(==4)

        size_t payload_bytes = (item_size == 3) ? 4 : item_size;

        if (offset + payload_bytes >= length)
        {
            break;
        }

        uint32_t item_value = 0;
        for (size_t i = 0; i < payload_bytes; i++)
        {
            item_value |= ((uint32_t)desc[offset + 1 + i]) << (8 * i);
        }

        if (item_type == 1) // Global
        {
            switch (item_tag)
            {
            case 0x00: // Usage Page
                usage_page = item_value;
                break;
            case 0x07: // Report Size
                report_size = item_value;
                break;
            case 0x08: // Report Count
                report_count = item_value;
                break;
            case 0x09: // Report ID
                current_report_id = (uint8_t)item_value;
                // initialize report_bits if first seen
                if (!report_used[current_report_id])
                {
                    report_used[current_report_id] = 1;
                    report_bits[current_report_id] = current_report_id ? 8 : 0;
                    tmp_layouts[current_report_id].report_id = current_report_id;
                }
                break;
            default:
                break;
            }
        }
        else if (item_type == 2) // Local
        {
            switch (item_tag)
            {
            case 0x00: // Usage
                last_usage = item_value;
                have_usage_min_max = false;
                break;
            case 0x01: // Usage Minimum
                usage_min = item_value;
                have_usage_min_max = true;
                break;
            case 0x02: // Usage Maximum
                usage_max = item_value;
                have_usage_min_max = true;
                break;
            default:
                break;
            }
        }
        else if (item_type == 0) // Main
        {
            switch (item_tag)
            {
            case 0x08: // Input
            {
                // Only consider Data fields (bit 0 == 1). Skip Constant fields.
                if ((item_value & 0x01) == 0)
                {
                    // advance bit offset anyway (constants also occupy bits)
                    if (report_used[current_report_id])
                        report_bits[current_report_id] += (uint32_t)report_count * (uint32_t)report_size;
                    last_usage = 0;
                    have_usage_min_max = false;
                    break;
                }

                // Ensure current report is marked
                if (!report_used[current_report_id])
                {
                    report_used[current_report_id] = 1;
                    report_bits[current_report_id] = current_report_id ? 8 : 0;
                    tmp_layouts[current_report_id].report_id = current_report_id;
                }

                uint32_t base_bit = report_bits[current_report_id];

                if (usage_page == 0x09)
                {
                    // Buttons
                    if (tmp_layouts[current_report_id].buttons_count == 0)
                    {
                        tmp_layouts[current_report_id].buttons_bit_offset = base_bit;
                        tmp_layouts[current_report_id].buttons_count = (uint16_t)report_count;
                    }
                }
                else if (usage_page == 0x01)
                {
                    if (have_usage_min_max)
                    {
                        for (uint32_t u = usage_min; u <= usage_max; u++)
                        {
                            if (u == 0x30) // X
                            {
                                if (tmp_layouts[current_report_id].x_size == 0)
                                {
                                    tmp_layouts[current_report_id].x_bit_offset = base_bit + (u - usage_min) * report_size;
                                    tmp_layouts[current_report_id].x_size = report_size;
                                }
                            }
                            else if (u == 0x31) // Y
                            {
                                if (tmp_layouts[current_report_id].y_size == 0)
                                {
                                    tmp_layouts[current_report_id].y_bit_offset = base_bit + (u - usage_min) * report_size;
                                    tmp_layouts[current_report_id].y_size = report_size;
                                }
                            }
                            else if (u == 0x38) // Wheel
                            {
                                if (tmp_layouts[current_report_id].wheel_size == 0)
                                {
                                    tmp_layouts[current_report_id].wheel_bit_offset = base_bit + (u - usage_min) * report_size;
                                    tmp_layouts[current_report_id].wheel_size = report_size;
                                }
                            }
                        }
                    }
                    else if (last_usage)
                    {
                        uint32_t u = last_usage;
                        if (u == 0x30) // X
                        {
                            if (tmp_layouts[current_report_id].x_size == 0)
                            {
                                tmp_layouts[current_report_id].x_bit_offset = base_bit;
                                tmp_layouts[current_report_id].x_size = report_size;
                            }
                        }
                        else if (u == 0x31) // Y
                        {
                            if (tmp_layouts[current_report_id].y_size == 0)
                            {
                                tmp_layouts[current_report_id].y_bit_offset = base_bit;
                                tmp_layouts[current_report_id].y_size = report_size;
                            }
                        }
                        else if (u == 0x38) // Wheel
                        {
                            if (tmp_layouts[current_report_id].wheel_size == 0)
                            {
                                tmp_layouts[current_report_id].wheel_bit_offset = base_bit;
                                tmp_layouts[current_report_id].wheel_size = report_size;
                            }
                        }
                    }
                }

                // advance bit offset
                report_bits[current_report_id] += (uint32_t)report_count * (uint32_t)report_size;

                // Clear local items per HID spec
                last_usage = 0;
                have_usage_min_max = false;
                break;
            }
            default:
                break;
            }
        }

        offset += 1 + payload_bytes;
    }

    // Select a single layout as legacy behavior: prefer a report that has buttons + x + y.
    for (int rid = 0; rid < 256; rid++)
    {
        if (report_used[rid])
        {
            tmp_layouts[rid].report_size_bits = report_bits[rid];
            // return first layout that has buttons + x + y
            if (tmp_layouts[rid].buttons_count > 0 && tmp_layouts[rid].x_size > 0 && tmp_layouts[rid].y_size > 0)
            {
                // copy to out
                memcpy(out_layout, &tmp_layouts[rid], sizeof(hid_report_layout_t));
                return 0;
            }
        }
    }

    // fallback: if any layout had data fields, return the first one
    for (int rid = 0; rid < 256; rid++)
    {
        if (report_used[rid])
        {
            memcpy(out_layout, &tmp_layouts[rid], sizeof(hid_report_layout_t));
            return -1;
        }
    }

    return -1;
}

int parse_hid_report_descriptor_layouts(const uint8_t *desc, size_t length, hid_report_layout_t *layouts, size_t max_layouts)
{
    if (desc == NULL || length == 0 || layouts == NULL || max_layouts == 0)
        return 0;

    // Reuse logic: build tmp per-report layouts similar to single-layout parser
    hid_report_layout_t tmp_layouts[256];
    uint8_t report_used[256] = {0};
    uint32_t report_bits[256];
    for (int i = 0; i < 256; i++)
    {
        memset(&tmp_layouts[i], 0, sizeof(hid_report_layout_t));
        report_bits[i] = 0;
    }

    size_t offset = 0;
    uint32_t usage_page = 0;
    uint32_t report_size = 0, report_count = 0;
    uint8_t current_report_id = 0;
    uint32_t last_usage = 0;
    uint32_t usage_min = 0, usage_max = 0;
    bool have_usage_min_max = false;

    while (offset < length)
    {
        uint8_t item = desc[offset];
        uint8_t item_type = (item >> 2) & 0x03;
        uint8_t item_tag = (item >> 4) & 0x0F;
        uint8_t item_size = item & 0x03;
        size_t payload_bytes = (item_size == 3) ? 4 : item_size;
        if (offset + payload_bytes >= length)
            break;
        uint32_t item_value = 0;
        for (size_t i = 0; i < payload_bytes; i++)
            item_value |= ((uint32_t)desc[offset + 1 + i]) << (8 * i);

        if (item_type == 1)
        {
            switch (item_tag)
            {
            case 0x00:
                usage_page = item_value;
                break;
            case 0x07:
                report_size = item_value;
                break;
            case 0x08:
                report_count = item_value;
                break;
            case 0x09:
                current_report_id = (uint8_t)item_value;
                if (!report_used[current_report_id])
                {
                    report_used[current_report_id] = 1;
                    report_bits[current_report_id] = current_report_id ? 8 : 0;
                    tmp_layouts[current_report_id].report_id = current_report_id;
                }
                break;
            default:
                break;
            }
        }
        else if (item_type == 2)
        {
            switch (item_tag)
            {
            case 0x00:
                last_usage = item_value;
                have_usage_min_max = false;
                break;
            case 0x01:
                usage_min = item_value;
                have_usage_min_max = true;
                break;
            case 0x02:
                usage_max = item_value;
                have_usage_min_max = true;
                break;
            default:
                break;
            }
        }
        else if (item_type == 0)
        {
            if (item_tag == 0x08)
            {
                // Input
                if ((item_value & 0x01) == 0)
                {
                    if (report_used[current_report_id])
                        report_bits[current_report_id] += (uint32_t)report_count * (uint32_t)report_size;
                }
                else
                {
                    if (!report_used[current_report_id])
                    {
                        report_used[current_report_id] = 1;
                        report_bits[current_report_id] = current_report_id ? 8 : 0;
                        tmp_layouts[current_report_id].report_id = current_report_id;
                    }
                    uint32_t base_bit = report_bits[current_report_id];
                    if (usage_page == 0x09)
                    {
                        if (tmp_layouts[current_report_id].buttons_count == 0)
                        {
                            tmp_layouts[current_report_id].buttons_bit_offset = base_bit;
                            tmp_layouts[current_report_id].buttons_count = (uint16_t)report_count;
                        }
                    }
                    else if (usage_page == 0x01)
                    {
                        if (have_usage_min_max)
                        {
                            for (uint32_t u = usage_min; u <= usage_max; u++)
                            {
                                if (u == 0x30 && tmp_layouts[current_report_id].x_size == 0)
                                {
                                    tmp_layouts[current_report_id].x_bit_offset = base_bit + (u - usage_min) * report_size;
                                    tmp_layouts[current_report_id].x_size = report_size;
                                }
                                else if (u == 0x31 && tmp_layouts[current_report_id].y_size == 0)
                                {
                                    tmp_layouts[current_report_id].y_bit_offset = base_bit + (u - usage_min) * report_size;
                                    tmp_layouts[current_report_id].y_size = report_size;
                                }
                                else if (u == 0x38 && tmp_layouts[current_report_id].wheel_size == 0)
                                {
                                    tmp_layouts[current_report_id].wheel_bit_offset = base_bit + (u - usage_min) * report_size;
                                    tmp_layouts[current_report_id].wheel_size = report_size;
                                }
                            }
                        }
                        else if (last_usage)
                        {
                            uint32_t u = last_usage;
                            if (u == 0x30 && tmp_layouts[current_report_id].x_size == 0)
                            {
                                tmp_layouts[current_report_id].x_bit_offset = base_bit;
                                tmp_layouts[current_report_id].x_size = report_size;
                            }
                            else if (u == 0x31 && tmp_layouts[current_report_id].y_size == 0)
                            {
                                tmp_layouts[current_report_id].y_bit_offset = base_bit;
                                tmp_layouts[current_report_id].y_size = report_size;
                            }
                            else if (u == 0x38 && tmp_layouts[current_report_id].wheel_size == 0)
                            {
                                tmp_layouts[current_report_id].wheel_bit_offset = base_bit;
                                tmp_layouts[current_report_id].wheel_size = report_size;
                            }
                        }
                    }
                    report_bits[current_report_id] += (uint32_t)report_count * (uint32_t)report_size;
                    last_usage = 0;
                    have_usage_min_max = false;
                }
            }
        }

        offset += 1 + payload_bytes;
    }

    // collect into output array
    size_t out_count = 0;
    for (int rid = 0; rid < 256 && out_count < (int)max_layouts; rid++)
    {
        if (report_used[rid])
        {
            tmp_layouts[rid].report_size_bits = report_bits[rid];
            layouts[out_count++] = tmp_layouts[rid];
        }
    }

    return (int)out_count;
}
