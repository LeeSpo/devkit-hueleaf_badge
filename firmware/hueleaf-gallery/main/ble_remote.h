#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define REMOTE_SERVICE_UUID "8f510001-6c8f-4d84-9af8-6e1f9000a001"
#define REMOTE_RX_UUID "8f510002-6c8f-4d84-9af8-6e1f9000a001"
#define REMOTE_TX_UUID "8f510003-6c8f-4d84-9af8-6e1f9000a001"
#define REMOTE_CHALLENGE_UUID "8f510004-6c8f-4d84-9af8-6e1f9000a001"
#define REMOTE_MESSAGE_MAX 512
/* Wire: version=1, op, request u16 LE, payload. Transport fragments below. */
enum { REMOTE_AUTHENTICATE=1, REMOTE_STATUS=2, REMOTE_LIST=3,
       REMOTE_NAV=4, REMOTE_SHOW=5, REMOTE_END=6 };
esp_err_t ble_remote_start(const uint8_t key[32], bool provisioned);
esp_err_t ble_remote_stop(void);
void ble_remote_slow(void);
bool ble_remote_pending(void);
/* Application hooks called in BLE task, never perform I/O here. */
bool remote_connected(void);
void remote_disconnected(void);
bool remote_authenticate(void);
bool remote_submit(const uint8_t *message, size_t len);
void ble_remote_reply(uint16_t id, uint8_t op, int status, const void *data, size_t len);
void ble_remote_disconnect(void);
