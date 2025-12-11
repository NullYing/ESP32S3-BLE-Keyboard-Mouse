#ifndef HID_REPORT_PARSER_H
#define HID_REPORT_PARSER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Parse and print HID Report Descriptor
 *
 * @param[in] desc    Pointer to HID Report Descriptor data
 * @param[in] length  Length of descriptor data
 */
void parse_hid_report_descriptor(const uint8_t *desc, size_t length);

/**
 * @brief Simple representation of parsed report layout for a mouse-like report
 */
typedef struct
{
    uint8_t report_id;           // 0 if no report id
    uint16_t buttons_count;      // number of button usages
    uint32_t buttons_bit_offset; // bit offset of first button
    uint32_t x_bit_offset;       // bit offset for X axis
    uint32_t x_size;             // size in bits
    uint32_t y_bit_offset;       // bit offset for Y axis
    uint32_t y_size;             // size in bits
    uint32_t wheel_bit_offset;   // bit offset for wheel
    uint32_t wheel_size;         // size in bits
    uint32_t report_size_bits;   // total report size in bits for this report id
} hid_report_layout_t;

/**
 * @brief Parse HID report descriptor and fill a simple layout struct for mouse-like reports.
 *
 * This function tries to find a report (optionally with Report ID) that contains Button usages
 * and X/Y/Wheel axis fields and fills the provided layout. It returns 0 on success, -1 if no
 * suitable mouse-like report was found.
 */
int parse_hid_report_descriptor_layout(const uint8_t *desc, size_t length, hid_report_layout_t *out_layout);

/**
 * @brief Parse HID report descriptor and fill multiple layouts (one per Report ID found).
 *
 * @param[in] desc       Descriptor bytes
 * @param[in] length     Descriptor length
 * @param[out] layouts   Array to fill with discovered layouts
 * @param[in] max_layouts Maximum number of entries allowed in `layouts`
 * @return number of layouts written to `layouts` (>=0). 0 means none found.
 *
 * This function is more general than `parse_hid_report_descriptor_layout` and will populate
 * layouts for each Report ID encountered. Caller should use the Report ID from the incoming
 * input reports to pick the correct layout.
 */
int parse_hid_report_descriptor_layouts(const uint8_t *desc, size_t length, hid_report_layout_t *layouts, size_t max_layouts);

#endif // HID_REPORT_PARSER_H
