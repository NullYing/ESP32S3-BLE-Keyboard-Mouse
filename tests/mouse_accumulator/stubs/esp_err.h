#ifndef TEST_ESP_ERR_H
#define TEST_ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_FAIL 2
static inline const char *esp_err_to_name(esp_err_t error) { (void)error; return "test"; }
#endif
