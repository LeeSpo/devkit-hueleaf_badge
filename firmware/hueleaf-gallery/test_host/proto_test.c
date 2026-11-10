#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform.h"

static int failure, locks, fat_calls, replaces, uninstalls, scans, probes;
static int64_t now_us = 123456000;
static bool ready, full_reply;
static uint8_t output[9000];
static size_t output_len, tx_calls;
static size_t tx_capacity = 512; /* ESP-IDF rejects a write larger than its TX ring. */
static int slideshow_writes, slideshow_error;
static bool saved_tap_wake;
static FILE *mock_fopen(const char *path, const char *mode) { (void)path; (void)mode; return tmpfile(); }
static int mock_fflush(FILE *f) { int r = fflush(f); return failure == 1 ? EOF : r; }
static int mock_fsync(int fd) { (void)fd; return failure == 2 ? -1 : 0; }
static size_t mock_fwrite(const void *p,size_t n,size_t count,FILE *f) { return failure == 4 ? 0 : fwrite(p,n,count,f); }
static int mock_fclose(FILE *f) { int r = fclose(f); return failure == 3 ? EOF : r; }
static int mock_unlink(const char *p) { (void)p; return 0; }
static int mock_mkdir(const char *p, mode_t m) { (void)p; (void)m; return 0; }
#define fopen mock_fopen
#define fflush mock_fflush
#define fsync mock_fsync
#define fclose mock_fclose
#define fwrite mock_fwrite
#define unlink mock_unlink
#define mkdir mock_mkdir
#include PROTO_SOURCE
#undef fopen
#undef fflush
#undef fsync
#undef fclose
#undef fwrite
#undef unlink
#undef mkdir

void slideshow_snapshot(slideshow_snapshot_t *s) { *s = (slideshow_snapshot_t){true, 30, "usb_paused", saved_tap_wake}; }
esp_err_t slideshow_set(bool enabled, uint32_t minutes)
{ assert(enabled && minutes == 30); slideshow_writes++; return slideshow_error; }
esp_err_t slideshow_set_options(bool enabled, uint32_t minutes, bool tap_wake)
{ if (!slideshow_error) saved_tap_wake = tap_wake; return slideshow_set(enabled, minutes); }

int usb_serial_jtag_write_bytes(const void *p, size_t n, int t)
{
    (void)t;
    tx_calls++;
    if (n > tx_capacity) return 0;
    assert(output_len + n <= sizeof(output));
    memcpy(output + output_len, p, n);
    output_len += n;
    return (int)n;
}
int usb_serial_jtag_read_bytes(void *p,size_t n,int t) { (void)p;(void)n;(void)t; return 0; }
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *c) { (void)c; return ESP_OK; }
esp_err_t usb_serial_jtag_driver_uninstall(void) { uninstalls++; return ESP_OK; }
int xTaskCreate(void (*f)(void *),const char *n,int s,void *a,int p,void *h) { (void)f;(void)n;(void)s;(void)a;(void)p;(void)h; return 0; }
void vTaskDelay(int t) { (void)t; }
void esp_restart(void) { abort(); }
int esp_reset_reason(void) { return 3; }
uint32_t esp_get_free_heap_size(void) { return 50000; }
uint32_t esp_get_minimum_free_heap_size(void) { return 40000; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 30000; }
int64_t esp_timer_get_time(void) { return now_us; }
void boot_select_restart_to_stock(void) { abort(); }
esp_err_t esp_vfs_fat_info(const char *p,uint64_t *a,uint64_t *b) { (void)p;(void)a;(void)b; fat_calls++; return ESP_OK; }
const esp_partition_t *esp_ota_get_running_partition(void) { return NULL; }
esp_err_t axp2101_gauge_status(bool *a,bool *b,int *c) { (void)a;(void)b;(void)c; return ESP_OK; }
esp_err_t pcf8563_set_time(const pcf8563_time_t *t) { (void)t; return ESP_OK; }
bool tf_card_is_mounted(void) { return true; }
esp_err_t tf_card_check(void) { probes++; return ESP_OK; }
bool tf_card_has_fault(void) { return false; }
esp_err_t gallery_selection_read(uint16_t start, uint64_t token, uint8_t *out, size_t cap, size_t *len)
{ assert(start==0 && token==0 && cap>=15); memset(out,0,15); out[0]=1; *len=15; return ESP_OK; }
esp_err_t gallery_selection_save(uint64_t token, uint16_t total, const uint8_t *bits, size_t len)
{ assert(token==1 && total==3 && len==1 && bits[0]==5); return ESP_OK; }
bool gallery_is_ready(void) { return ready; }
bool gallery_upload_session_begin(void) { return runtime_session_begin(now_us / 1000); }
bool gallery_upload_file_begin(void) { return runtime_file_begin(now_us / 1000); }
void sd_lock(void) { locks++; }
void sd_unlock(void) {}
void gallery_get_state(int *c,int *n,char *s,size_t z,bool *b) { (void)c;(void)n;(void)s;(void)z;(void)b; }
void gallery_list(void (*cb)(const char *,void *),void *p) {
    if (full_reply) for (int i=0;i<300;i++) cb("01234567890123456789012345678901234567890123456789012345678.bin",p);
}
void gallery_label_path(char *p,size_t n,const char *s) { snprintf(p,n,"/labels/%s",s); }
int gallery_rescan(bool redraw) { assert(!redraw); scans++; return 0; }
esp_err_t gallery_show_name(const char *n) { (void)n; return ESP_OK; }
void gallery_nav(int n) { (void)n; }
void gallery_set_strip(bool b) { (void)b; }
size_t logbuf_read(char *p,size_t n) {
    if (!full_reply) return 0;
    for (size_t i=0;i<n;i++) p[i]=(char)(i*29+7);
    return n;
}
bool file_store_replace(const char *tmp,const char *dst) { assert(strstr(tmp,".upload.tmp")); assert(!strcmp(dst,GALLERY_DIR "/photo.bin")); replaces++; return true; }
bool file_store_recover(const char *p) { (void)p; return true; }

static int status(int cmd,const uint8_t *p,uint32_t n) {
    runtime_set_ready(ready); output_len=0; tx_calls=0;
    /* Exercise actual RX/response aliasing, not a separate input buffer. */
    assert(n<=PROTO_MAX_PAYLOAD);
    if (n) memmove(s_packet+12,p,n);
    handle(cmd, 42, s_packet+12, n);
    assert(output_len>=20 && !memcmp(output,"HLP1",4));
    assert(output[4]==(cmd|0x80) && output[5]==42);
    assert(output_len == 16u + get32(output+8));
    assert(get32(output+output_len-4)==proto_crc32(0,output+4,output_len-8));
    return (int32_t)get32(output+12);
}
static void begin(void) {
    uint8_t p[14]={0}, zeros[SCREEN_FRAME_BYTES]={0};
    put32(p+1,SCREEN_FRAME_BYTES); put32(p+5,proto_crc32(0,zeros,sizeof(zeros))); memcpy(p+9,"photo",5);
    assert(status(CMD_UP_BEGIN,p,sizeof(p))==0);
}
static void complete(void) {
    uint8_t p[4096]={0};
    while (s_up_off < s_up_size) {
        uint32_t n = s_up_size - s_up_off;
        if (n > sizeof(p)-4) n = sizeof(p)-4;
        put32(p,s_up_off);
        assert(status(CMD_UP_DATA,p,n+4)==0);
    }
    assert(status(CMD_UP_END,NULL,0)==(failure ? -7 : 0));
}
int main(void) {
    uint8_t p[16]={0};
    assert(status(CMD_LIST,NULL,0)==-8);
    assert(status(CMD_UP_BEGIN,p,10)==-8);
    assert(status(CMD_INFO,NULL,0)==0 && locks==0 && fat_calls==0);
    assert(output_len > tx_capacity && tx_calls > 1);
    output[output_len-4]=0;
    assert(strstr((char *)output+16,"\"upload_no_refresh\":true"));
    assert(strstr((char *)output+16,"\"uptime_ms\":123456"));
    assert(strstr((char *)output+16,"\"reset_reason\":3"));
    ready=true; begin();
    FILE *old=s_up; uint32_t oldsize=s_up_size, oldcrc=s_up_crc; char final[sizeof(s_up_final)]; strcpy(final,s_up_final);
    assert(status(CMD_UP_BEGIN,p,10)==-5);
    assert(s_up==old && s_up_size==oldsize && s_up_crc==oldcrc && !strcmp(s_up_final,final));
    s_up_off=UINT32_MAX-1; s_up_size=UINT32_MAX; put32(p,s_up_off);
    assert(status(CMD_UP_DATA,p,8)==-5 && s_up_off==UINT32_MAX-1);
    s_up_size=oldsize; s_up_off=0; complete(); assert(replaces==1 && scans==1);
    /* Consecutive uploads (including overwrites) do not request a display or
     * restart: those stubs abort if ever reached. */
    for (int i=0; i<2; i++) { begin(); complete(); }
    assert(replaces==3 && scans==3);
    for (failure=1; failure<=3; failure++) { begin(); complete(); assert(replaces==3 && scans==3); }
    failure=0;
    int oldlocks=locks, oldprobes=probes;
    assert(runtime_display_begin());
    assert(status(CMD_INFO,NULL,0)==0);
    assert(status(CMD_PING,NULL,0)==0 && status(CMD_LOG,NULL,0)==0);
    assert(status(CMD_LIST,NULL,0)==-9);
    assert(status(CMD_UP_BEGIN,p,10)==-9);
    assert(status(CMD_SHOW,(const uint8_t *)"photo",5)==-9);
    assert(status(CMD_STRIP,p,1)==-9);
    assert(locks==oldlocks && probes==oldprobes && fat_calls==0);
    runtime_display_end();
    uint8_t start=1, end=0, alive=2;
    assert(status(CMD_UPLOAD_SESSION,&start,1)==0);
    assert(status(CMD_UPLOAD_SESSION,&alive,1)==0);
    assert(status(CMD_NAV,NULL,0)==-9);
    assert(status(CMD_DELETE,(const uint8_t *)"photo",5)==-9);
    assert(status(CMD_SET_ORDER,NULL,0)==-9);
    for (int i=0; i<3; i++) { begin(); complete(); assert(runtime_input_blocked()); }
    assert(status(CMD_UPLOAD_SESSION,&end,1)==0 && !runtime_input_blocked());
    assert(status(CMD_UPLOAD_SESSION,&end,1)==0);
    assert(status(CMD_UPLOAD_SESSION,&alive,1)==-5);
    assert(status(CMD_UPLOAD_SESSION,&start,1)==0);
    begin();
    int oldreplaces=replaces;
    assert(status(CMD_UPLOAD_SESSION,&end,1)==-9);
    assert(status(CMD_REBOOT,NULL,0)==-9);
    now_us += 121000000;
    assert(status(CMD_INFO,NULL,0)==0);
    assert(!s_up && !runtime_input_blocked() && replaces==oldreplaces);
    begin(); complete();  /* A retry after expiry uses a fresh temporary file. */
    failure=4; begin(); put32(p,0);
    assert(status(CMD_UP_DATA,p,8)==-7 && !s_up && !runtime_input_blocked());
    failure=0;
    uint8_t slide[5] = {1, 30, 0, 0, 0};
    assert(status(CMD_SLIDESHOW, slide, 5) == 0 && slideshow_writes == 1);
    assert(status(CMD_SLIDESHOW, slide, 4) == -5);
    slide[0] = 2; assert(status(CMD_SLIDESHOW, slide, 5) == -5); slide[0] = 1;
    put32(slide + 1, UINT32_MAX); assert(status(CMD_SLIDESHOW, slide, 5) == -5);
    put32(slide + 1, 0); assert(status(CMD_SLIDESHOW, slide, 5) == -5); put32(slide + 1, 30);
    assert(runtime_session_begin(now_us / 1000)); assert(status(CMD_SLIDESHOW, slide, 5) == -9); runtime_session_end();
    slideshow_error = ESP_FAIL; assert(status(CMD_SLIDESHOW, slide, 5) == -7);
    slideshow_error = ESP_ERR_NO_MEM; assert(status(CMD_SLIDESHOW, slide, 5) == -10); slideshow_error = 0;
    assert(runtime_sleep_begin()); assert(status(CMD_SLIDESHOW, slide, 5) == -9); runtime_sleep_end();
    uint8_t extended[6] = {1, 30, 0, 0, 0, 1};
    assert(status(CMD_SLIDESHOW, extended, 6) == 0 && saved_tap_wake);
    assert(status(CMD_SLIDESHOW, slide, 5) == 0 && saved_tap_wake); /* legacy preserves it */
    extended[5] = 2; assert(status(CMD_SLIDESHOW, extended, 6) == -5);
    extended[5] = 0; assert(status(CMD_SLIDESHOW, extended, 6) == 0 && !saved_tap_wake);
    uint8_t selection_read[10]={0}, selection_save[11]={1,0,0,0,0,0,0,0,3,0,5};
    assert(status(CMD_SLIDESHOW_SELECTION,selection_read,10)==0);
    assert(status(CMD_SLIDESHOW_SELECTION,selection_read,9)==-5);
    assert(status(CMD_SET_SLIDESHOW_SELECTION,selection_save,11)==0);
    assert(status(CMD_SET_SLIDESHOW_SELECTION,selection_save,10)==-5);
    assert(runtime_session_begin(now_us/1000));
    assert(status(CMD_SET_SLIDESHOW_SELECTION,selection_save,11)==-9); runtime_session_end();
    assert(runtime_display_begin());
    assert(status(CMD_SLIDESHOW_SELECTION,selection_read,10)==-9); runtime_display_end();
    assert(runtime_sleep_begin());
    assert(status(CMD_SET_SLIDESHOW_SELECTION,selection_save,11)==-9); runtime_sleep_end();
    full_reply=true;
    assert(status(CMD_LIST,NULL,0)==0);
    assert(output_len>8000 && output_len<=sizeof(s_packet));
    assert(!memcmp(output+16,"0123456789",10));
    assert(status(CMD_LOG,NULL,0)==0);
    assert(output_len==20+PROTO_MAX_PAYLOAD-8);
    for (size_t i=0;i<PROTO_MAX_PAYLOAD-8;i++) assert(output[16+i]==(uint8_t)(i*29+7));
    full_reply=false;
    /* Maximum-size request and duplicate ACK: CRC must see original input. */
    begin();
    static uint8_t max_data[PROTO_MAX_PAYLOAD];
    assert(status(CMD_UP_DATA,max_data,sizeof(max_data))==0 && s_up_off==PROTO_MAX_PAYLOAD-4);
    assert(status(CMD_UP_DATA,max_data,sizeof(max_data))==0 && s_up_off==PROTO_MAX_PAYLOAD-4);
    complete();
    /* reply() can also move response data from inside an RX packet. */
    for (size_t i=0;i<PROTO_MAX_PAYLOAD;i++) max_data[i]=(uint8_t)(i*13+5);
    memcpy(s_packet+12,max_data,PROTO_MAX_PAYLOAD);
    output_len=0;
    reply(CMD_LOG,42,0,s_packet+12,REPLY_DATA_MAX);
    assert(output_len==sizeof(s_packet) && !memcmp(output+16,max_data,REPLY_DATA_MAX));
    assert(get32(output+output_len-4)==proto_crc32(0,output+4,output_len-8));
    /* Exercise TX ring boundaries, trailing chunks and full binary payloads. */
    const size_t lengths[] = {492, 512, 513, 4096, REPLY_DATA_MAX};
    for (size_t i=0; i<sizeof(lengths)/sizeof(lengths[0]); i++) {
        output_len=0; tx_calls=0;
        reply(CMD_LOG,42,0,max_data,lengths[i]);
        assert(output_len==20+lengths[i] && tx_calls>1);
        assert(!memcmp(output+16,max_data,lengths[i]));
        assert(get32(output+output_len-4)==proto_crc32(0,output+4,output_len-8));
    }
    /* A disconnected/full ring must stop sending, not spin forever. */
    output_len=0; tx_calls=0; tx_capacity=0;
    reply(CMD_PING,42,0,NULL,0);
    assert(output_len==0 && tx_calls==1);
    tx_capacity=512;
    assert(proto_start()==ESP_ERR_NO_MEM && uninstalls==1);
    return 0;
}
