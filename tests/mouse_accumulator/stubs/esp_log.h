#ifndef TEST_ESP_LOG_H
#define TEST_ESP_LOG_H
#include <stdio.h>
#define ESP_LOGI(tag, ...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#endif
