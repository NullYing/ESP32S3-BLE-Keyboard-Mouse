#include "led_control.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "soc/soc_caps.h"
#include <stdio.h>

static const char *TAG_LED = "LED";

/**
 * @brief 初始化LED控制模块
 *
 * @return led_strip_handle_t LED条带句柄，如果失败返回NULL
 */
led_strip_handle_t led_control_init(void) {
  // LED strip general initialization, according to your led board design
  led_strip_config_t strip_config = {
      .strip_gpio_num =
          LED_GPIO_PIN, // The GPIO that connected to the LED strip's data line
      .max_leds = 1,    // The number of LEDs in the strip,
      .led_model = LED_MODEL_WS2812, // LED strip model
      .color_component_format =
#ifdef CONFIG_STATUS_LED_COLOR_ORDER_GRB
          LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#else
          LED_STRIP_COLOR_COMPONENT_FMT_RGB,
#endif
      .flags = {
#ifdef CONFIG_STATUS_LED_INVERT_OUT
          .invert_out = true,
#else
          .invert_out = false,
#endif
      }};

  // LED strip backend configuration: RMT
  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,  // different clock source can lead to
                                       // different power consumption
      .resolution_hz = LED_RMT_RES_HZ, // RMT counter clock frequency
      .mem_block_symbols =
          SOC_RMT_MEM_WORDS_PER_CHANNEL,
      .flags = {
          .with_dma =
              false, // DMA feature is available on chips like ESP32-S3/P4
      }};

  // LED Strip object handle
  led_strip_handle_t led_strip;
  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  ESP_ERROR_CHECK(led_strip_clear(led_strip));
  ESP_LOGI(TAG_LED, "Created LED strip object with RMT backend");
  return led_strip;
}

/**
 * @brief 根据USB和BLE HID连接状态设置LED颜色
 *
 * @param led_strip LED条带句柄
 * @param usb_keyboard_connected USB键盘是否已连接
 * @param usb_mouse_connected USB鼠标是否已连接
 * @param ble_connected BLE HID是否已连接
 */
void led_control_set_color(led_strip_handle_t led_strip,
                           bool usb_keyboard_connected,
                           bool usb_mouse_connected, bool ble_connected) {
  if (led_strip == NULL) {
    return;
  }

  bool usb_device_connected = (usb_keyboard_connected || usb_mouse_connected);
  printf("USB HID: %s (键盘:%s, 鼠标:%s), BLE HID: %s\n",
         usb_device_connected ? "已连接" : "未连接",
         usb_keyboard_connected ? "是" : "否",
         usb_mouse_connected ? "是" : "否",
         ble_connected ? "已连接" : "未连接");

  esp_err_t ret = ESP_OK;

  if (usb_device_connected && ble_connected) {
    // USB设备已连接且BLE已连接 - 白色
    ret = led_strip_set_pixel(led_strip, 0, LED_BRIGHTNESS, LED_BRIGHTNESS,
                              LED_BRIGHTNESS);
  } else if (usb_device_connected) {
    // USB设备已连接但BLE未连接 - 绿色
    ret = led_strip_set_pixel(led_strip, 0, 0, LED_BRIGHTNESS, 0);
  } else if (ble_connected) {
    // BLE已连接但USB设备未连接 - 蓝色
    ret = led_strip_set_pixel(led_strip, 0, 0, 0, LED_BRIGHTNESS);
  } else {
    // 都未连接 - 红色
    ret = led_strip_set_pixel(led_strip, 0, LED_BRIGHTNESS, 0, 0);
  }

  if (ret != ESP_OK) {
    ESP_LOGW(TAG_LED, "设置LED像素失败: %s", esp_err_to_name(ret));
    return;
  }

  ret = led_strip_refresh(led_strip);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG_LED, "刷新LED条带失败: %s", esp_err_to_name(ret));
  }
}

/**
 * @brief 闪烁蓝色LED表示正在切换BLE设备
 *
 * @param led_strip LED条带句柄
 */
void led_control_blink_switching(led_strip_handle_t led_strip) {
  if (led_strip == NULL) {
    return;
  }

  ESP_LOGI(TAG_LED, "LED 闪烁：正在切换BLE设备...");

  // 快速闪烁蓝色3次表示切换中
  for (int i = 0; i < 3; i++) {
    // 蓝色亮
    led_strip_set_pixel(led_strip, 0, 0, 0, LED_BRIGHTNESS);
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 灭
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // 保持蓝色常亮表示等待连接
  led_strip_set_pixel(led_strip, 0, 0, 0, LED_BRIGHTNESS);
  led_strip_refresh(led_strip);
}
