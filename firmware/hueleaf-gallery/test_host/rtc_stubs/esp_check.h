#ifndef ESP_CHECK_H
#define ESP_CHECK_H
#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { (void)(tag); esp_err_t e_ = (expr); if (e_ != ESP_OK) return e_; } while (0)
#endif
