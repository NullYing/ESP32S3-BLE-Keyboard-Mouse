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

#endif // HID_REPORT_PARSER_H
