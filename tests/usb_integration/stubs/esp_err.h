#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_ERR_NOT_FOUND 3
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "stub"; }
