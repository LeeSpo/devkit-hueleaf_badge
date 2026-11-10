#include "ble_remote.h"
#include <string.h>
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "mbedtls/md.h"

#define UUID(n) BLE_UUID128_INIT(0x01,0xa0,0x00,0x90,0x1f,0x6e,0xf8,0x9a,0x84,0x4d,0x8f,0x6c,n,0x00,0x51,0x8f)
static const ble_uuid128_t service_uuid = UUID(1), rx_uuid = UUID(2), tx_uuid = UUID(3), challenge_uuid = UUID(4);
static uint8_t s_key[32], s_challenge[32], s_rx[REMOTE_MESSAGE_MAX], s_addr_type;
static size_t s_rx_len;
static uint8_t s_rx_seq, s_rx_stream;
static bool s_provisioned, s_started, s_subscribed, s_authenticated, s_stopping, s_slow;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE, s_tx_handle;
static int64_t s_fragment_deadline;
static SemaphoreHandle_t s_lock, s_stopped;
static int gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void);
void ble_store_config_init(void);

/* TX uses stop-and-wait indications: retain one response until acknowledged. */
static uint8_t s_tx[REMOTE_MESSAGE_MAX];
static size_t s_tx_len, s_tx_off, s_tx_next;
static uint8_t s_tx_seq, s_tx_stream;
static bool s_tx_busy;
static int64_t s_tx_deadline;
static uint8_t s_auth_request[36];
static bool s_auth_cached;
static void send_next_locked(void)
{
    if (!s_tx_busy || s_conn == BLE_HS_CONN_HANDLE_NONE || !s_subscribed) return;
    uint16_t mtu = ble_att_mtu(s_conn);
    size_t room = mtu > 7 ? mtu - 7 : 16;
    if (room > 244) room = 244;
    size_t count = s_tx_len - s_tx_off;
    if (count > room) count = room;
    uint8_t fragment[248];
    fragment[0] = s_tx_stream; fragment[1] = s_tx_seq;
    fragment[2] = (s_tx_off == 0 ? 1 : 0) | (s_tx_off + count == s_tx_len ? 2 : 0);
    fragment[3] = 0;
    memcpy(fragment + 4, s_tx + s_tx_off, count);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(fragment, count + 4);
    if (!om) { s_tx_busy = false; return; }
    s_tx_next = s_tx_off + count;
    if (ble_gatts_indicate_custom(s_conn, s_tx_handle, om) != 0) s_tx_busy = false;
}
bool ble_remote_pending(void)
{
    if (!s_lock) return false;
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    bool busy = s_tx_busy;
    bool expired = busy && esp_timer_get_time()/1000 >= s_tx_deadline;
    if (expired) s_tx_busy = busy = false;
    xSemaphoreGiveRecursive(s_lock);
    if (expired) ble_remote_disconnect();
    return busy;
}
void ble_remote_reply(uint16_t id, uint8_t op, int status, const void *data, size_t len)
{
    if (!s_lock || len > REMOTE_MESSAGE_MAX - 6) return;
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    if (!s_tx_busy && s_conn != BLE_HS_CONN_HANDLE_NONE && s_subscribed) {
        s_tx[0] = 1; s_tx[1] = op | 0x80; s_tx[2] = id; s_tx[3] = id >> 8;
        s_tx[4] = status; s_tx[5] = (uint16_t)status >> 8;
        if (len) memcpy(s_tx + 6, data, len);
        s_tx_len = len + 6; s_tx_off = s_tx_seq = 0; s_tx_stream++;
        s_tx_deadline = esp_timer_get_time()/1000 + 10000;
        s_tx_busy = true; send_next_locked();
    }
    xSemaphoreGiveRecursive(s_lock);
}
static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)attr;
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_CHR && ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    if (arg) {
        uint8_t challenge[33];
        challenge[0] = s_provisioned ? 1 : 0;
        memcpy(challenge + 1, s_challenge, 32);
        return os_mbuf_append(ctx->om, challenge, sizeof(challenge)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    uint8_t f[248]; uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctx->om, f, sizeof(f), &n) || n < 5 || f[3] || (f[2] & ~3)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    if (!s_subscribed || s_tx_busy) {
        ESP_LOGW("ble_remote", "RX rejected: subscribed=%d tx_busy=%d", s_subscribed, s_tx_busy);
        xSemaphoreGiveRecursive(s_lock); return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    int64_t now = esp_timer_get_time() / 1000;
    if (f[2] & 1) {
        if (f[1] != 0) { xSemaphoreGiveRecursive(s_lock); return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; }
        s_rx_len = 0; s_rx_seq = 0; s_rx_stream = f[0]; s_fragment_deadline = now + 5000;
    }
    if (now >= s_fragment_deadline || f[0] != s_rx_stream || f[1] != s_rx_seq || s_rx_len + n - 4 > sizeof(s_rx)) {
        s_rx_len = 0; xSemaphoreGiveRecursive(s_lock); return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    memcpy(s_rx + s_rx_len, f + 4, n - 4); s_rx_len += n - 4; s_rx_seq++;
    if (!(f[2] & 2)) { xSemaphoreGiveRecursive(s_lock); return 0; }
    size_t size = s_rx_len; s_rx_len = 0; s_fragment_deadline = 0;
    if (size < 4 || s_rx[0] != 1) { xSemaphoreGiveRecursive(s_lock); return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; }
    uint16_t id = s_rx[2] | (uint16_t)s_rx[3] << 8;
    uint8_t op = s_rx[1];
    if (op == REMOTE_AUTHENTICATE) {
        if (s_authenticated && s_auth_cached && size == sizeof(s_auth_request) && !memcmp(s_auth_request, s_rx, size)) {
            xSemaphoreGiveRecursive(s_lock); ble_remote_reply(id, op, 0, NULL, 0); return 0;
        }
        struct ble_gap_conn_desc desc;
        bool encrypted = ble_gap_conn_find(conn, &desc) == 0 && desc.sec_state.encrypted;
        uint8_t expected[32] = {0};
        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        int rc = mbedtls_md_hmac(md, s_key, sizeof(s_key), s_challenge, sizeof(s_challenge), expected);
        unsigned diff = 0;
        for (unsigned i = 0; i < 32; i++) diff |= expected[i] ^ (size == 36 ? s_rx[4 + i] : 0);
        bool ok = encrypted && !s_authenticated && s_provisioned && size == 36 && !rc && !diff && remote_authenticate();
        ESP_LOGI("ble_remote", "authentication: encrypted=%d accepted=%d", encrypted, ok);
        if (ok) { memcpy(s_auth_request, s_rx, sizeof(s_auth_request)); s_auth_cached = true; s_authenticated = true; }
        esp_fill_random(s_challenge, sizeof(s_challenge));
        xSemaphoreGiveRecursive(s_lock);
        ble_remote_reply(id, op, ok ? 0 : -10, NULL, 0);
        return 0;
    }
    bool ok = s_authenticated && remote_submit(s_rx, size);
    xSemaphoreGiveRecursive(s_lock);
    if (!ok) ble_remote_reply(id, op, s_authenticated ? -9 : -10, NULL, 0);
    return 0;
}
static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid=&rx_uuid.u, .access_cb=access_cb, .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
        {.uuid=&tx_uuid.u, .access_cb=access_cb, .val_handle=&s_tx_handle, .flags=BLE_GATT_CHR_F_INDICATE},
        {.uuid=&challenge_uuid.u, .access_cb=access_cb, .arg=(void *)1, .flags=BLE_GATT_CHR_F_READ},
        {0}
    }}, {0}};
static void advertise(void)
{
    if (s_stopping || s_conn != BLE_HS_CONN_HANDLE_NONE) return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid; fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields)) return;
    struct ble_hs_adv_fields scan = {0};
    scan.name = (uint8_t *)ble_svc_gap_device_name(); scan.name_len = strlen((char *)scan.name); scan.name_is_complete = 1;
    if (ble_gap_adv_rsp_set_fields(&scan)) return;
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND; params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = s_slow ? 1280 : 160; params.itvl_max = s_slow ? 1600 : 240;
    ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) { advertise(); break; }
        xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
        s_conn = event->connect.conn_handle; s_authenticated = s_subscribed = s_auth_cached = false;
        s_rx_len = 0; s_tx_busy = false; s_fragment_deadline = 0;
        esp_fill_random(s_challenge, sizeof(s_challenge));
        xSemaphoreGiveRecursive(s_lock);
        if (!remote_connected()) ble_remote_disconnect();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
        s_conn = BLE_HS_CONN_HANDLE_NONE; s_authenticated = s_subscribed = s_tx_busy = s_auth_cached = false;
        s_rx_len = 0;
        xSemaphoreGiveRecursive(s_lock);
        remote_disconnected(); advertise(); break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* A phone may have discarded its keys. Application HMAC authentication
         * is still required after replacing this transport-only bond. */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) != 0)
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI("ble_remote", "encryption status=%d", event->enc_change.status);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI("ble_remote", "subscription: handle=%u tx=%u indicate=%d", event->subscribe.attr_handle, s_tx_handle, event->subscribe.cur_indicate);
        xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
        /* Android also subscribes to GATT Service Changed (including restored
         * bond CCCDs). That must not clear the remote TX subscription. */
        if (event->subscribe.attr_handle == s_tx_handle)
            s_subscribed = event->subscribe.cur_indicate;
        xSemaphoreGiveRecursive(s_lock); break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        if (event->notify_tx.indication && event->notify_tx.status != 0) {
            xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
            if (event->notify_tx.status == BLE_HS_EDONE && s_tx_busy) {
                s_tx_deadline = esp_timer_get_time()/1000 + 10000;
                s_tx_off = s_tx_next; s_tx_seq++;
                if (s_tx_off == s_tx_len) s_tx_busy = false; else send_next_locked();
            } else s_tx_busy = false;
            xSemaphoreGiveRecursive(s_lock);
        }
        break;
    default: break;
    }
    return 0;
}
static void sync_cb(void)
{
    if (!ble_hs_util_ensure_addr(0) && !ble_hs_id_infer_auto(0, &s_addr_type)) advertise();
}
static void host_task(void *arg)
{
    (void)arg; nimble_port_run(); xSemaphoreGive(s_stopped); vTaskSuspend(NULL);
}
esp_err_t ble_remote_start(const uint8_t key[32], bool provisioned)
{
    if (s_started) return ESP_OK;
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_stopped) s_stopped = xSemaphoreCreateBinary();
    if (!s_lock || !s_stopped) return ESP_ERR_NO_MEM;
    memcpy(s_key, key, 32); s_provisioned = provisioned; s_stopping = s_slow = false;
    esp_err_t rc = nimble_port_init();
    if (rc != ESP_OK) return rc;
    ble_hs_cfg.sync_cb = sync_cb;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_sc = 1; ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_svc_gap_device_name_set("HueLeaf Remote");
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) { nimble_port_deinit(); return ESP_FAIL; }
    s_started = true; nimble_port_freertos_init(host_task); return ESP_OK;
}
void ble_remote_slow(void)
{
    if (s_started && !s_slow) { s_slow = true; if (s_conn == BLE_HS_CONN_HANDLE_NONE) { ble_gap_adv_stop(); advertise(); } }
}
void ble_remote_disconnect(void)
{
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
}
esp_err_t ble_remote_stop(void)
{
    if (!s_started) return ESP_OK;
    s_stopping = true;
    ble_gap_adv_stop();
    int rc = nimble_port_stop();
    if (rc || xSemaphoreTake(s_stopped, pdMS_TO_TICKS(3000)) != pdTRUE) return ESP_FAIL;
    nimble_port_freertos_deinit();
    esp_err_t err = nimble_port_deinit();
    if (err == ESP_OK) { s_started = false; s_conn = BLE_HS_CONN_HANDLE_NONE; s_tx_busy = false; }
    return err;
}
