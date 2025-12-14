idf_component_register(
  SRCS
  "hid_host_example.c"
  "esp_hidd_prf_api.c"
  "hid_dev.c"
  "hid_device_le_prf.c"
  "hid_report_parser.c"
  "hid_device_type_detector.c"
  "mouse_accumulator.c"
  "led_control.c"
  INCLUDE_DIRS
  "."
  PRIV_REQUIRES
  usb
  esp_driver_gpio
  bt
  spi_flash
  esp_psram
  nvs_flash
  esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)
