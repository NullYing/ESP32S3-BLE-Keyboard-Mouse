#ifndef TEST_ESP_TIMER_H
#define TEST_ESP_TIMER_H
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); void *arg; int dispatch_method; const char *name; } esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t interval);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
#endif
